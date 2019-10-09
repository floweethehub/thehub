/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
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
#include "TransactionBuilder.h"

#include <primitives/pubkey.h>
#include <primitives/key.h>

TransactionBuilder::TransactionBuilder()
{
}

TransactionBuilder::TransactionBuilder(const Tx &existingTx)
    : m_transaction(existingTx.createOldTransaction())
{
    m_signInfo.resize(m_transaction.vin.size());
}

TransactionBuilder::TransactionBuilder(const CTransaction &existingTx)
    : m_transaction(existingTx)
{
    m_signInfo.resize(m_transaction.vin.size());
}

int TransactionBuilder::appendInput(const uint256 &txid, int outputIndex)
{
    const size_t pos = m_transaction.vin.size();
    if (pos > 1000) // kind of random large number
        throw std::runtime_error("Too many inputs");
    m_transaction.vin.resize(pos + 1);
    m_signInfo.resize(pos + 1);
    CTxIn &in = m_transaction.vin[pos];
    in.prevout.hash = txid;
    in.prevout.n = outputIndex;
    switch (m_defaultLocking) {
    case TransactionBuilder::LockMiningOnTime:
    case TransactionBuilder::LockMiningOnBlock:
        in.nSequence = in.SEQUENCE_LOCKTIME_DISABLE_FLAG;
        break;
    default: // default of the instance is fine
        break;
    }
    m_curInput = static_cast<int>(pos);
    return m_curInput;
}

int TransactionBuilder::selectInput(int index)
{
    assert(index >= 0);
    if (index < 0) throw std::runtime_error("Index is a natural number");
    m_curInput = std::min(static_cast<int>(m_transaction.vin.size()) -1, index);
    return m_curInput;
}

void TransactionBuilder::pushInputSignature(const CKey &privKey, const CScript &prevOutScript, int64_t amount, SignInputs inputs, SignOutputs outputs)
{
    checkCurInput();
    SignInfo &si = m_signInfo[m_curInput];
    si.hashType = inputs == SignOnlyThisInput ? 0xC0 : 0x40;
    switch (outputs) {
    case SignAllOuputs: si.hashType += 1; break;
    case SignNoOutputs: si.hashType += 2; break;
    case SignSingleOutput: si.hashType += 3; break;
    }

    si.privKey = privKey;
    si.prevOutScript = prevOutScript;
    si.amount = amount;
}

void TransactionBuilder::deleteInput(int index)
{
    assert(index >= 0);
    assert(index < m_transaction.vin.size());
    assert(index < m_signInfo.size());
    auto iter = m_transaction.vin.begin();
    iter += index;
    m_transaction.vin.erase(iter);

    auto iter2 = m_signInfo.begin();
    iter2 += index;
    m_signInfo.erase(iter2);

    selectInput(index);
}

int TransactionBuilder::appendOutput(int64_t amount)
{
    const size_t pos = m_transaction.vout.size();
    if (pos > 1000) // kind of random large number
        throw std::runtime_error("Too many outputs");
    m_transaction.vout.resize(pos + 1);
    CTxOut &out = m_transaction.vout[pos];
    out.nValue = amount;

    m_curOutput = static_cast<int>(pos);
    return m_curOutput;
}

int TransactionBuilder::selectOutput(int index)
{
    assert(index >= 0);
    if (index < 0) throw std::runtime_error("Index is a natural number");
    m_curOutput = std::min(static_cast<int>(m_transaction.vout.size()) -1, index);
    return m_curOutput;
}

void TransactionBuilder::pushOutputPay2Address(const CKeyID &address)
{
    checkCurOutput();
    CScript outScript;
    outScript << OP_DUP << OP_HASH160;
    std::vector<unsigned char> data(address.begin(), address.end());
    outScript << data;
    outScript << OP_EQUALVERIFY << OP_CHECKSIG;
    pushOutputScript(outScript);
}

void TransactionBuilder::pushOutputScript(const CScript &script)
{
    assert(m_curOutput >= 0);
    assert(static_cast<size_t>(m_curOutput) < m_transaction.vout.size());
    m_transaction.vout[static_cast<size_t>(m_curOutput)].scriptPubKey = script;
}

void TransactionBuilder::deleteOutput(int index)
{
    assert(index >= 0);
    assert(index < m_transaction.vout.size());
    auto iter = m_transaction.vout.begin();
    iter+=index;
    m_transaction.vout.erase(iter);
    selectOutput(index);
}

Tx TransactionBuilder::createTransaction(Streaming::BufferPool *pool)
{
    // sign all inputs we can.
    assert(m_transaction.vin.size() == m_signInfo.size());
    for (size_t i = 0; i < m_transaction.vin.size(); ++i) {
        const SignInfo &si = m_signInfo[i];
        if (si.prevOutScript.empty())
            continue;

        uint256 hashPrevouts;
        if (!(si.hashType & SignOnlyThisInput)) {
            CHashWriter ss(SER_GETHASH, 0);
            for (size_t n = 0; n < m_transaction.vin.size(); ++n) {
                ss << m_transaction.vin[n].prevout;
            }
            hashPrevouts = ss.GetHash();
        }
        uint256 hashSequence;
        if (!(si.hashType & SignOnlyThisInput) && (si.hashType & 0x1f) != SignSingleOutput
                && (si.hashType & 0x1f) != SignNoOutputs) {
            CHashWriter ss(SER_GETHASH, 0);
            for (size_t n = 0; n < m_transaction.vin.size(); ++n) {
                ss << m_transaction.vin[n].nSequence;
            }
            hashSequence = ss.GetHash();
        }
        uint256 hashOutputs;
        if ((si.hashType & 0x1f) != SignSingleOutput && (si.hashType & 0x1f) != SignNoOutputs) {
            CHashWriter ss(SER_GETHASH, 0);
            for (size_t n = 0; n < m_transaction.vout.size(); ++n) {
                ss << m_transaction.vout[n];
            }
            hashOutputs = ss.GetHash();
        } else if ((si.hashType & 0x1f) == SignSingleOutput && i < m_transaction.vout.size()) {
            CHashWriter ss(SER_GETHASH, 0);
            ss << m_transaction.vout[i];
            hashOutputs = ss.GetHash();
        }

        // use FORKID based creation of the hash we will sign.
        CHashWriter ss(SER_GETHASH, 0);
        ss << m_transaction.nVersion << hashPrevouts << hashSequence;
        ss << m_transaction.vin[i].prevout;
        ss << static_cast<const CScriptBase &>(si.prevOutScript);
        ss << si.amount << m_transaction.vin[i].nSequence << hashOutputs;
        ss << m_transaction.nLockTime << (int) si.hashType;
        const uint256 hash = ss.GetHash();

        // the rest assumes P2PKH for now.
        std::vector<unsigned char> vchSig;
        si.privKey.Sign(hash, vchSig);
        vchSig.push_back((uint8_t) si.hashType);

        m_transaction.vin[i].scriptSig = CScript();
        m_transaction.vin[i].scriptSig << vchSig;
        m_transaction.vin[i].scriptSig << ToByteVector(si.privKey.GetPubKey());
    }

    return Tx::fromOldTransaction(m_transaction, pool);
}

void TransactionBuilder::checkCurInput()
{
    assert(0 <= m_curInput);
    assert(m_transaction.vin.size() > m_curInput);
    if (0 > m_curInput || m_transaction.vin.size() <= m_curInput)
        throw std::runtime_error("current input out of range");
}

void TransactionBuilder::checkCurOutput()
{
    assert(m_curOutput >= 0);
    assert(m_curOutput < m_transaction.vout.size());
}
