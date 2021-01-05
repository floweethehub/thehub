/*
 * This file is part of the Flowee project
 * Copyright (C) 2019-2020 Tom Zander <tomz@freedommail.ch>
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

#include <hash.h>
#include <primitives/pubkey.h>
#include <primitives/transaction.h>


class TransactionBuilderPrivate
{
public:
    void checkCurInput();
    void checkCurOutput();

    CMutableTransaction transaction;

    TransactionBuilder::LockingOptions defaultLocking = TransactionBuilder::NoLocking;
    int curInput = -1, curOutput = -1;

    struct SignInfo {
        uint8_t hashType = 0;
        int64_t amount = 0;
        CKey privKey;
        CScript prevOutScript;
    };
    std::vector<SignInfo> signInfo;
};


TransactionBuilder::TransactionBuilder()
    : d(new TransactionBuilderPrivate())
{
}

TransactionBuilder::TransactionBuilder(const Tx &existingTx)
    : d(new TransactionBuilderPrivate())
{
    d->transaction = CTransaction(existingTx.createOldTransaction());
    d->signInfo.resize(d->transaction.vin.size());
}

TransactionBuilder::TransactionBuilder(const CTransaction &existingTx)
    : d(new TransactionBuilderPrivate())
{
    d->transaction = CTransaction(existingTx);
    d->signInfo.resize(d->transaction.vin.size());
}

TransactionBuilder::~TransactionBuilder()
{
    delete d;
}

int TransactionBuilder::appendInput(const uint256 &txid, int outputIndex)
{
    const size_t pos = d->transaction.vin.size();
    if (pos > 1000) // kind of random large number
        throw std::runtime_error("Too many inputs");
    d->transaction.vin.resize(pos + 1);
    d->signInfo.resize(pos + 1);
    CTxIn &in = d->transaction.vin[pos];
    in.prevout.hash = txid;
    in.prevout.n = outputIndex;
    switch (d->defaultLocking) {
    case TransactionBuilder::LockMiningOnTime:
    case TransactionBuilder::LockMiningOnBlock:
        in.nSequence = in.SEQUENCE_LOCKTIME_DISABLE_FLAG;
        break;
    default: // default of the instance is fine
        break;
    }
    d->curInput = static_cast<int>(pos);
    return d->curInput;
}

int TransactionBuilder::selectInput(int index)
{
    assert(index >= 0);
    if (index < 0) throw std::runtime_error("Index is a natural number");
    d->curInput = std::min(static_cast<int>(d->transaction.vin.size()) -1, index);
    return d->curInput;
}

int TransactionBuilder::outputCount() const
{
    return static_cast<int>(d->transaction.vout.size());
}

int TransactionBuilder::inputCount() const
{
    return static_cast<int>(d->transaction.vin.size());
}

void TransactionBuilder::pushInputSignature(const CKey &privKey, const CScript &prevOutScript, int64_t amount, SignInputs inputs, SignOutputs outputs)
{
    d->checkCurInput();
    TransactionBuilderPrivate::SignInfo &si = d->signInfo[d->curInput];
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
    assert(size_t(index) < d->transaction.vin.size());
    assert(size_t(index) < d->signInfo.size());
    auto iter = d->transaction.vin.begin();
    iter += index;
    d->transaction.vin.erase(iter);

    auto iter2 = d->signInfo.begin();
    iter2 += index;
    d->signInfo.erase(iter2);

    selectInput(index);
}

int TransactionBuilder::appendOutput(int64_t amount)
{
    const size_t pos = d->transaction.vout.size();
    if (pos > 1000) // kind of random large number
        throw std::runtime_error("Too many outputs");
    d->transaction.vout.resize(pos + 1);
    CTxOut &out = d->transaction.vout[pos];
    out.nValue = amount;

    d->curOutput = static_cast<int>(pos);
    return d->curOutput;
}

int TransactionBuilder::selectOutput(int index)
{
    assert(index >= 0);
    if (index < 0) throw std::runtime_error("Index is a natural number");
    d->curOutput = std::min(static_cast<int>(d->transaction.vout.size()) -1, index);
    return d->curOutput;
}

void TransactionBuilder::setOutputValue(int64_t value)
{
    assert(value >= 0);
    assert(d->curOutput >= 0);
    assert(int(d->transaction.vout.size()) > d->curOutput);
    d->transaction.vout[d->curOutput].nValue = value;
}

void TransactionBuilder::pushOutputPay2Address(const CKeyID &address)
{
    d->checkCurOutput();
    CScript outScript;
    outScript << OP_DUP << OP_HASH160;
    std::vector<unsigned char> data(address.begin(), address.end());
    outScript << data;
    outScript << OP_EQUALVERIFY << OP_CHECKSIG;
    pushOutputScript(outScript);
}

void TransactionBuilder::pushOutputScript(const CScript &script)
{
    assert(d->curOutput >= 0);
    assert(static_cast<size_t>(d->curOutput) < d->transaction.vout.size());
    d->transaction.vout[static_cast<size_t>(d->curOutput)].scriptPubKey = script;
}

void TransactionBuilder::deleteOutput(int index)
{
    assert(index >= 0);
    assert(size_t(index) < d->transaction.vout.size());
    auto iter = d->transaction.vout.begin();
    iter+=index;
    d->transaction.vout.erase(iter);
    selectOutput(index);
}

Tx TransactionBuilder::createTransaction(Streaming::BufferPool *pool)
{
    // sign all inputs we can.
    assert(d->transaction.vin.size() == d->signInfo.size());
    for (size_t i = 0; i < d->transaction.vin.size(); ++i) {
        const TransactionBuilderPrivate::SignInfo &si = d->signInfo[i];
        if (si.prevOutScript.empty())
            continue;

        uint256 hashPrevouts;
        if (!(si.hashType & SignOnlyThisInput)) {
            CHashWriter ss(SER_GETHASH, 0);
            for (size_t n = 0; n < d->transaction.vin.size(); ++n) {
                ss << d->transaction.vin[n].prevout;
            }
            hashPrevouts = ss.GetHash();
        }
        uint256 hashSequence;
        if (!(si.hashType & SignOnlyThisInput) && (si.hashType & 0x1f) != SignSingleOutput
                && (si.hashType & 0x1f) != SignNoOutputs) {
            CHashWriter ss(SER_GETHASH, 0);
            for (size_t n = 0; n < d->transaction.vin.size(); ++n) {
                ss << d->transaction.vin[n].nSequence;
            }
            hashSequence = ss.GetHash();
        }
        uint256 hashOutputs;
        if ((si.hashType & 0x1f) != SignSingleOutput && (si.hashType & 0x1f) != SignNoOutputs) {
            CHashWriter ss(SER_GETHASH, 0);
            for (size_t n = 0; n < d->transaction.vout.size(); ++n) {
                ss << d->transaction.vout[n];
            }
            hashOutputs = ss.GetHash();
        } else if ((si.hashType & 0x1f) == SignSingleOutput && i < d->transaction.vout.size()) {
            CHashWriter ss(SER_GETHASH, 0);
            ss << d->transaction.vout[i];
            hashOutputs = ss.GetHash();
        }

        // use FORKID based creation of the hash we will sign.
        CHashWriter ss(SER_GETHASH, 0);
        ss << d->transaction.nVersion << hashPrevouts << hashSequence;
        ss << d->transaction.vin[i].prevout;
        ss << static_cast<const CScriptBase &>(si.prevOutScript);
        ss << si.amount << d->transaction.vin[i].nSequence << hashOutputs;
        ss << d->transaction.nLockTime << (int) si.hashType;
        const uint256 hash = ss.GetHash();

        // the rest assumes P2PKH for now.
        std::vector<unsigned char> vchSig;
        si.privKey.Sign(hash, vchSig);
        vchSig.push_back((uint8_t) si.hashType);

        d->transaction.vin[i].scriptSig = CScript();
        d->transaction.vin[i].scriptSig << vchSig;
        d->transaction.vin[i].scriptSig << ToByteVector(si.privKey.GetPubKey());
    }

    return Tx::fromOldTransaction(d->transaction, pool);
}

void TransactionBuilderPrivate::checkCurInput()
{
    assert(0 <= curInput);
    assert(transaction.vin.size() > size_t(curInput));
    if (0 > curInput || transaction.vin.size() <= size_t(curInput))
        throw std::runtime_error("current input out of range");
}

void TransactionBuilderPrivate::checkCurOutput()
{
    assert(curOutput >= 0);
    assert(size_t(curOutput) < transaction.vout.size());
}
