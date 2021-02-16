/*
 * This file is part of the Flowee project
 * Copyright (C) 2017-2021 Tom Zander <tom@flowee.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "FastTransaction.h"
#include "FastBlock.h"
#include "TxIterator_p.h"
#include "transaction.h"

#include <compat/endian.h>

#include <hash.h>
#include <streaming/streams.h>

Tx::Tx()
{
}

Tx::Tx(const Streaming::ConstBuffer &rawTransaction)
    : m_data(rawTransaction)
{
}

uint32_t Tx::txVersion() const
{
    return static_cast<uint32_t>(le32toh(*((uint32_t*)m_data.begin())));
}

uint256 Tx::createHash() const
{
    CHash256 ctx;
    ctx.Write((const unsigned char*) m_data.begin(), m_data.size());
    uint256 result;
    ctx.Finalize((unsigned char*)&result);
    return result;
}

CTransaction Tx::createOldTransaction() const
{
    CTransaction answer;
    CDataStream buf(m_data.begin(), m_data.end(), 0 , 0);
    answer.Unserialize(buf, 0, 0);
    return answer;
}

int64_t Tx::offsetInBlock(const FastBlock &block) const
{
    assert(m_data.isValid());
    assert(block.data().isValid());
    assert(block.data().internal_buffer() == m_data.internal_buffer());
    return m_data.begin() - block.data().begin();
}

Tx Tx::fromOldTransaction(const CTransaction &transaction, Streaming::BufferPool *pool)
{
    CSizeComputer sc(0, 0);
    sc << transaction;
    if (pool) {
        pool->reserve(sc.size());
        transaction.Serialize(*pool, 0, 0);
        return Tx(pool->commit());
    }
    Streaming::BufferPool pl(sc.size());
    transaction.Serialize(pl, 0, 0);
    return Tx(pl.commit());
}

// static
std::list<Tx::Input> Tx::findInputs(Tx::Iterator &iter)
{
    std::list<Input> inputs;

    Input curInput;
    auto content = iter.next();
    while (content != Tx::End) {
        if (content == Tx::PrevTxHash) { // only read inputs for non-coinbase txs
            if (iter.byteData().size() != 32)
                throw std::runtime_error("Failed to understand PrevTxHash");
            curInput.txid = iter.uint256Data();
            content = iter.next(Tx::PrevTxIndex);
            if (content != Tx::PrevTxIndex)
                throw std::runtime_error("Failed to find PrevTxIndex");
            curInput.index = iter.intData();
            inputs.push_back(curInput);
        }
        else if (content == Tx::OutputValue) {
            break;
        }
        content = iter.next();
    }
    return inputs;
}

// static
Tx::Output Tx::nextOutput(Tx::Iterator &iter)
{
    Output answer;
    auto content = iter.tag();
    while (content != Tx::End) {
        if (content == Tx::OutputValue) {
            answer.outputValue = static_cast<int64_t>(iter.longData());
            content = iter.next();
            if (content != Tx::OutputScript)
                throw std::runtime_error("Malformed transaction");
            answer.outputScript = iter.byteData();
            break;
        }
        content = iter.next(Tx::OutputValue);
    }
    return answer;
}

Tx::Output Tx::output(int index) const
{
    assert(index >= 0);
    Output answer;
    Iterator iter(*this);
    auto content = iter.next(Tx::OutputValue);
    while (content != Tx::End) {
        if (index-- == 0) {
            answer.outputValue = static_cast<int64_t>(iter.longData());
            content = iter.next();
            if (content != Tx::OutputScript)
                throw std::runtime_error("Malformed transaction");
            answer.outputScript = iter.byteData();
            break;
        }
        content = iter.next(Tx::OutputValue);
    }
    return answer;
}


////////////////////////////////////////////////////////////

bool static isConstBytes(Tx::Component tag)
{
    return tag == Tx::TxVersion || tag == Tx::LockTime || tag == Tx::PrevTxIndex || tag == Tx::Sequence;
}

TxTokenizer::TxTokenizer(const Streaming::ConstBuffer &buffer)
    : m_data(buffer),
    m_txStart(m_data.begin()),
    m_currentTokenStart(m_txStart),
    m_currentTokenEnd(m_txStart),
    m_tag(Tx::End)
{
}

TxTokenizer::TxTokenizer(const FastBlock &block, int offsetInBlock)
    : m_data(block.data()),
    m_txStart(nullptr),
    m_tag(Tx::End)
{
    assert(block.isFullBlock());
    const char *pos;
    if (offsetInBlock == 0) {
        pos = m_data.begin() + 80;
        pos += readCompactSizeSize(pos); // num tx field
    } else {
        pos = m_data.begin() + offsetInBlock;
    }
    m_currentTokenStart = pos;
    m_currentTokenEnd = pos;
}

Tx::Component TxTokenizer::next() {
    if (m_currentTokenEnd + 1 >= m_data.end()) {
        m_tag = Tx::End;
        return tag();
    }
    m_currentTokenStart = m_currentTokenEnd;
    if (m_currentTokenStart == m_txStart || m_tag == Tx::End) {
        m_txStart = m_currentTokenStart;
        m_currentTokenEnd += 4;
        m_tag = Tx::TxVersion;
        return checkSpaceForTag();
    }
    bool startInput = false, startOutput = false;
    if (m_currentTokenStart == m_txStart + 4) {
        uint64_t x = readCompactSize(&m_currentTokenEnd, m_data.end());
        if (x > 0xFFFF)
            throw std::runtime_error("Tx invalid");
        m_numInputsLeft = static_cast<int>(x);
        // we immediately go to the next token
        m_currentTokenStart = m_currentTokenEnd;
        startInput = true;
    }
    if (m_tag == Tx::Sequence) {
        if (--m_numInputsLeft > 0) {
            startInput = true;
        } else {
            uint64_t x = readCompactSize(&m_currentTokenEnd, m_data.end());
            if (x > 0xFFFF)
                throw std::runtime_error("Tx invalid");
            m_numOutputsLeft = static_cast<int>(x);
            // we immediately go to the next token
            m_currentTokenStart = m_currentTokenEnd;
            startOutput = true;
        }
    }
    if (startInput) {
        m_currentTokenEnd += 32;
        m_tag = Tx::PrevTxHash;
        return checkSpaceForTag();
    }
    if (m_tag == Tx::PrevTxHash) {
        m_currentTokenEnd += 4;
        m_tag = Tx::PrevTxIndex;
        return checkSpaceForTag();
    }

    if (m_tag == Tx::PrevTxIndex) {
        uint64_t scriptLength = readCompactSize(&m_currentTokenEnd, m_data.end());
        if (scriptLength > 0xFFFF)
            throw std::runtime_error("Tx invalid");
        m_currentTokenStart = m_currentTokenEnd;
        m_currentTokenEnd += static_cast<int>(scriptLength);
        m_tag = Tx::TxInScript;
        return checkSpaceForTag();
    }
    if (m_tag == Tx::TxInScript) {
        m_currentTokenEnd += 4;
        m_tag = Tx::Sequence;
        return checkSpaceForTag();
    }
    if (m_tag == Tx::OutputScript) {
        if (--m_numOutputsLeft > 0) {
            startOutput = true;
        } else {
            m_currentTokenEnd += 4;
            m_tag = Tx::LockTime;
            return checkSpaceForTag();
        }
    }
    if (startOutput) {
        m_currentTokenEnd += 8;
        m_tag = Tx::OutputValue;
        return checkSpaceForTag();
    }
    if (m_tag == Tx::OutputValue) {
        uint64_t scriptLength = readCompactSize(&m_currentTokenEnd, m_data.end());
        if (scriptLength > 0xFFFF)
            throw std::runtime_error("Tx invalid");
        m_currentTokenStart = m_currentTokenEnd;
        m_currentTokenEnd += static_cast<int>(scriptLength);
        m_tag = Tx::OutputScript;
        return checkSpaceForTag();
    }
    if (m_tag == Tx::LockTime)
        m_tag = Tx::End;
    else
        assert(false);
    return tag();
}

Tx::Component TxTokenizer::checkSpaceForTag()
{
    if (m_tag != Tx::End && m_currentTokenEnd > m_data.end())
        throw std::runtime_error("Tx data missing");
    return tag();
}

Tx::Iterator::Iterator(const Tx &tx)
    : d(new TxTokenizer(tx.m_data))
{
}

Tx::Iterator::Iterator(const FastBlock &block, int offsetInBlock)
    : d(new TxTokenizer(block, offsetInBlock))
{
}

Tx::Iterator::Iterator(const Tx::Iterator && other)
{
    d = std::move(other.d);
}

Tx::Iterator::~Iterator()
{
    delete d;
}

Tx::Component Tx::Iterator::next(int filter)
{
    do {
         int tag = d->next();
         if (filter == 0 || (tag & filter))
             return static_cast<Tx::Component>(tag);
    } while (d->tag() != Tx::End);
    return Tx::End;
}

Tx::Component Tx::Iterator::tag() const
{
    return d->tag();
}

Tx Tx::Iterator::prevTx() const
{
    assert(d->tag() == Tx::End);
    return Tx(Streaming::ConstBuffer(d->m_data.internal_buffer(), d->m_txStart, d->m_currentTokenEnd));
}

Streaming::ConstBuffer Tx::Iterator::byteData() const
{
    return Streaming::ConstBuffer(d->m_data.internal_buffer(), d->m_currentTokenStart, d->m_currentTokenEnd);
}

int Tx::Iterator::dataLength() const
{
    return d->m_currentTokenEnd - d->m_currentTokenStart;
}

int32_t Tx::Iterator::intData() const
{
    if (isConstBytes(d->tag()))
        return uintData();
    const char *tmp = d->m_currentTokenStart;
    return readCompactSize(&tmp, d->m_data.end());
}

uint32_t Tx::Iterator::uintData() const
{
    if (isConstBytes(d->tag()))
        return le32toh(*((uint32_t*)(d->m_currentTokenStart)));
    const char *tmp = d->m_currentTokenStart;
    return readCompactSize(&tmp, d->m_data.end());
}

uint64_t Tx::Iterator::longData() const
{
    if (d->tag() == OutputValue)
        return le64toh(*((uint64_t*)(d->m_currentTokenStart)));
    if (isConstBytes(d->tag()))
        return le32toh(*((uint32_t*)(d->m_currentTokenStart)));
    const char *tmp = d->m_currentTokenStart;
    return readCompactSize(&tmp, d->m_data.end());
}

uint256 Tx::Iterator::uint256Data() const
{
    assert (d->m_currentTokenEnd - d->m_currentTokenStart >= 32);
    uint256 answer(d->m_currentTokenStart);
    return answer;
}

void Tx::Iterator::hashByteData(uint256 &output) const
{
    CSHA256 hasher;
    hasher.Write(d->m_currentTokenStart, dataLength());
    uint256 buf;
    hasher.Finalize(output.begin());
}

bool operator==(const Tx::Input &a, const Tx::Input &b)
{
    return a.index == b.index && a.dataFile == b.dataFile && a.txid == b.txid;
}
