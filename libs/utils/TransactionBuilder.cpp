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

TransactionBuilder::TransactionBuilder()
{
}

TransactionBuilder::TransactionBuilder(const Tx &existingTx)
    : m_transaction(existingTx.createOldTransaction())
{
}

TransactionBuilder::TransactionBuilder(const CTransaction &existingTx)
    : m_transaction(existingTx)
{
}

int TransactionBuilder::appendInput(const uint256 &txid, int outputIndex)
{
    const size_t pos = m_transaction.vin.size();
    if (pos > 1000) // kind of random large number
        throw std::runtime_error("Too many inputs");
    m_transaction.vin.resize(pos + 1);
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
    m_curInput = std::min(static_cast<int>(m_transaction.vin.size()) -1, index);
    return m_curInput;
}

void TransactionBuilder::setSignatureOption(TransactionBuilder::SignInputs inputs, TransactionBuilder::SignOutputs outputs)
{
    assert(0 <= m_curInput);
    assert(m_transaction.vin.size() > m_curInput);
    if (0 > m_curInput || m_transaction.vin.size() <= m_curInput)
        throw std::runtime_error("current input out of range");

    uint32_t sigHash = inputs == SignOnlyThisInput ? 0xC0 : 0x40;
    switch (outputs) {
    case TransactionBuilder::SignAllOuputs:
        sigHash += 1;
        break;
    case TransactionBuilder::SignNoOutputs:
        sigHash += 2;
        break;
    case TransactionBuilder::SignSingleOutput:
        sigHash += 3;
        break;
    }
    CScript &script = m_transaction.vin[m_curInput].scriptSig;
    if (script.size() > 0) { // it already has data.
        if (script[script.size()-1] == sigHash) // no change
            return;
        // TODO remember we removed the signature?
    }
    script << sigHash;
}

void TransactionBuilder::deleteInput(int index)
{
    assert(index >= 0);
    assert(index < m_transaction.vin.size());
    auto iter = m_transaction.vin.begin();
    iter+=index;
    m_transaction.vin.erase(iter);
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
    m_curOutput = std::min(static_cast<int>(m_transaction.vout.size()) -1, index);
    return m_curOutput;
}

void TransactionBuilder::setPublicKeyHash(const CPubKey &address)
{
    assert(m_curOutput >= 0);
    assert(m_curOutput < m_transaction.vout.size());
    CScript outScript;
    outScript << OP_DUP << OP_HASH160;
    std::vector<unsigned char> data(address.begin(), address.end());
    outScript << data;
    outScript << OP_EQUALVERIFY << OP_CHECKSIG;
    m_transaction.vout[m_curOutput].scriptPubKey = outScript;
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

Tx TransactionBuilder::createTransaction(Streaming::BufferPool *pool) const
{
    return Tx::fromOldTransaction(m_transaction, pool);
}
