/*
 * This file is part of the Flowee project
 * Copyright (C) 2019-2021 Tom Zander <tom@flowee.org>
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

#include "DoubleSpendProof.h"
#include "UnspentOutputData.h"
#include "txmempool.h"
#include "script/interpreter.h"

#include <utxo/UnspentOutputDatabase.h>
#include <hash.h>
#include <primitives/pubkey.h>
#include <fstream>
#include <streaming/P2PParser.h>

namespace {
    enum ScriptType {
        P2PKH
    };

    void getP2PKHSignature(const CScript &script, std::vector<uint8_t> &vchRet)
    {
        auto scriptIter = script.begin();
        opcodetype type;
        script.GetOp(scriptIter, type, vchRet);
    }

    void hashTx(DoubleSpendProof::Spender &spender, const CTransaction &tx, int inputIndex)
    {
        assert(!spender.pushData.empty());
        assert(!spender.pushData.front().empty());
        auto hashType = spender.pushData.front().back();
        if (!(hashType & SIGHASH_ANYONECANPAY)) {
            CHashWriter ss(SER_GETHASH, 0);
            for (size_t n = 0; n < tx.vin.size(); ++n) {
                ss << tx.vin[n].prevout;
            }
            spender.hashPrevOutputs = ss.finalizeHash();
        }
        if (!(hashType & SIGHASH_ANYONECANPAY) && (hashType & 0x1f) != SIGHASH_SINGLE
                && (hashType & 0x1f) != SIGHASH_NONE) {
            CHashWriter ss(SER_GETHASH, 0);
            for (size_t n = 0; n < tx.vin.size(); ++n) {
                ss << tx.vin[n].nSequence;
            }
            spender.hashSequence = ss.finalizeHash();
        }
        if ((hashType & 0x1f) != SIGHASH_SINGLE && (hashType & 0x1f) != SIGHASH_NONE) {
            CHashWriter ss(SER_GETHASH, 0);
            for (size_t n = 0; n < tx.vout.size(); ++n) {
                ss << tx.vout[n];
            }
            spender.hashOutputs = ss.finalizeHash();
        } else if ((hashType & 0x1f) == SIGHASH_SINGLE && inputIndex < int(tx.vout.size())) {
            CHashWriter ss(SER_GETHASH, 0);
            ss << tx.vout[inputIndex];
            spender.hashOutputs = ss.finalizeHash();
        }
    }

    class DSPSignatureChecker : public BaseSignatureChecker {
    public:
        DSPSignatureChecker(const DoubleSpendProof *proof, const DoubleSpendProof::Spender &spender, int64_t amount)
            : m_proof(proof),
              m_spender(spender),
              m_amount(amount)
        {
        }

        bool CheckSig(const std::vector<uint8_t> &vchSigIn, const std::vector<uint8_t> &vchPubKey, const CScript &scriptCode, uint32_t /*flags*/) const override {
            CPubKey pubkey(vchPubKey);
            if (!pubkey.IsValid())
                return false;

            std::vector<uint8_t> vchSig(vchSigIn);
            if (vchSig.empty())
                return false;
            vchSig.pop_back(); // drop the hashtype byte tacked on to the end of the signature

            CHashWriter ss(SER_GETHASH, 0);
            ss << m_spender.txVersion << m_spender.hashPrevOutputs << m_spender.hashSequence;
            ss <<  COutPoint(m_proof->prevTxId(), m_proof->prevOutIndex());
            ss << static_cast<const CScriptBase &>(scriptCode);
            ss << m_amount << m_spender.outSequence << m_spender.hashOutputs;
            ss << m_spender.lockTime << (int) m_spender.pushData.front().back();
            const uint256 sighash = ss.finalizeHash();

            if (vchSig.size() == 64)
                return pubkey.verifySchnorr(sighash, vchSig);
            return pubkey.verifyECDSA(sighash, vchSig);
        }
        bool CheckLockTime(const CScriptNum&) const override {
            return true;
        }
        bool CheckSequence(const CScriptNum&) const override {
            return true;
        }

        const DoubleSpendProof *m_proof;
        const DoubleSpendProof::Spender &m_spender;
        const int64_t m_amount;
    };
}

// static
DoubleSpendProof DoubleSpendProof::create(const Tx &tx1, const Tx &tx2)
{
    DoubleSpendProof answer;
    Spender &s1 = answer.m_spender1;
    Spender &s2 = answer.m_spender2;

    CTransaction t1 = tx1.createOldTransaction();
    CTransaction t2 = tx2.createOldTransaction();

    size_t inputIndex1 = 0;
    size_t inputIndex2 = 0;
    bool found = false;
    for (;!found && inputIndex1 < t1.vin.size(); ++inputIndex1) {
        const CTxIn &in1 = t1.vin.at(inputIndex1);
        for (inputIndex2 = 0; inputIndex2 < t2.vin.size(); ++inputIndex2) {
            const CTxIn &in2 = t2.vin.at(inputIndex2);
            if (in1.prevout == in2.prevout) {
                answer.m_prevOutIndex = in1.prevout.n;
                answer.m_prevTxId = in1.prevout.hash;

                s1.outSequence = in1.nSequence;
                s2.outSequence = in2.nSequence;

                // TODO pass in the mempool and find the prev-tx we spend
                // then we can determine what script type we are dealing with and
                // be smarter about finding the signature.
                // Assume p2pkh for now.

                s1.pushData.resize(1);
                getP2PKHSignature(in1.scriptSig, s1.pushData.front());
                s2.pushData.resize(1);
                getP2PKHSignature(in2.scriptSig, s2.pushData.front());

                assert(!s1.pushData.empty()); // we resized it
                assert(!s2.pushData.empty()); // we resized it
                if (s1.pushData.front().empty() || s2.pushData.front().empty())
                    throw std::runtime_error("scriptSig has no signature");
                auto hashType = s1.pushData.front().back();
                if (!(hashType & SIGHASH_FORKID))
                    throw std::runtime_error("Tx1 Not a Bitcoin Cash P2PKH transaction");
                hashType = s2.pushData.front().back();
                if (!(hashType & SIGHASH_FORKID))
                    throw std::runtime_error("Tx2 Not a Bitcoin Cash P2PKH transaction");

                found = true;
                break;
            }
        }
    }


    if (!found)
        throw std::runtime_error("Transactions do not double spend each other");
    if (s1.pushData.front().empty() || s2.pushData.front().empty())
        throw std::runtime_error("Transactions not using known payment type. Could not find sig");

    s1.txVersion = t1.nVersion;
    s2.txVersion = t2.nVersion;
    s1.lockTime = t1.nLockTime;
    s2.lockTime = t2.nLockTime;

    hashTx(s1, t1, inputIndex1);
    hashTx(s2, t2, inputIndex2);

    // sort the spenders so the proof stays the same, independent of the order of tx seen first
    int diff = s1.hashOutputs.Compare(s2.hashOutputs);
    if (diff == 0)
        diff = s1.hashPrevOutputs.Compare(s2.hashPrevOutputs);
    if (diff > 0)
        std::swap(s1, s2);

    // Finally, ensure that we can eat our own dog food -- this should always succeed,
    // it is a programming error if it does not.
    answer.checkSanityOrThrow();

    return answer;
}

DoubleSpendProof DoubleSpendProof::load(const Streaming::ConstBuffer &buffer)
{
    DoubleSpendProof dsp;
    Streaming::P2PParser parser(buffer);

    dsp.m_prevTxId = parser.readUint256();
    dsp.m_prevOutIndex = parser.readInt();
    dsp.m_spender1.txVersion = parser.readInt();
    dsp.m_spender1.outSequence = parser.readInt();
    dsp.m_spender1.lockTime = parser.readInt();
    dsp.m_spender1.hashPrevOutputs = parser.readUint256();
    dsp.m_spender1.hashSequence = parser.readUint256();
    dsp.m_spender1.hashOutputs = parser.readUint256();
    int count = parser.readCompactInt();
    for (int i = 0; i < count; ++i) {
        int vectorSize = parser.readCompactInt();
        dsp.m_spender1.pushData.push_back(parser.readUnsignedBytes(vectorSize));
    }

    dsp.m_spender2.txVersion = parser.readInt();
    dsp.m_spender2.outSequence = parser.readInt();
    dsp.m_spender2.lockTime = parser.readInt();
    dsp.m_spender2.hashPrevOutputs = parser.readUint256();
    dsp.m_spender2.hashSequence = parser.readUint256();
    dsp.m_spender2.hashOutputs = parser.readUint256();
    count = parser.readCompactInt();
    for (int i = 0; i < count; ++i) {
        int vectorSize = parser.readCompactInt();
        dsp.m_spender2.pushData.push_back(parser.readUnsignedBytes(vectorSize));
    }

    return dsp;
}

DoubleSpendProof::DoubleSpendProof()
{
}

bool DoubleSpendProof::isEmpty() const
{
    return m_prevOutIndex == -1 || m_prevTxId.IsNull();
}

DoubleSpendProof::Validity DoubleSpendProof::validate(const CTxMemPool &mempool) const
{
    if (m_prevTxId.IsNull() || m_prevOutIndex < 0)
        return Invalid;
    if (m_spender1.pushData.empty() || m_spender1.pushData.front().empty()
            || m_spender2.pushData.empty() || m_spender2.pushData.front().empty())
        return Invalid;

    try { // Check basics. No DSP should ever violate those.
        checkSanityOrThrow();
    } catch (const std::runtime_error &e) {
        return Invalid;
    }

    // check if ordering is proper
    int diff = m_spender1.hashOutputs.Compare(m_spender2.hashOutputs);
    if (diff == 0)
        diff = m_spender1.hashPrevOutputs.Compare(m_spender2.hashPrevOutputs);
    if (diff > 0)
        return Invalid;

    // Get the previous output we are spending.
    int64_t amount;
    CScript prevOutScript;
    Tx prevTx;
    if (mempool.lookup(m_prevTxId, prevTx)) {
        auto output = prevTx.output(m_prevOutIndex);
        if (output.outputValue < 0 || output.outputScript.isEmpty())
            return Invalid;
        amount = output.outputValue;
        prevOutScript = output.outputScript;
    } else {
        auto prevTxData = mempool.utxo()->find(m_prevTxId, m_prevOutIndex);
        if (!prevTxData.isValid()) {
            /* if the output we spend is missing then either the tx just got mined
             * or, more likely, our mempool just doesn't have it.
             */
            return MissingUTXO;
        }
        UnspentOutputData data(prevTxData);
        amount = data.outputValue();
        prevOutScript = data.outputScript();
    }

    /*
     * Find the matching transaction spending this. Possibly identical to one
     * of the sides of this DSP.
     * We need this because we want the public key that it contains.
     */
    Tx tx;
    if (!mempool.lookup(COutPoint(m_prevTxId, m_prevOutIndex), tx)) {
        return MissingTransaction;
    }

    /*
     * TZ: At this point (2019-07) we only support P2PKH payments.
     *
     * Since we have an actually spending tx, we could trivially support various other
     * types of scripts because all we need to do is replace the signature from our 'tx'
     * with the one that comes from the DSP.
     */
    ScriptType scriptType = P2PKH; // TODO look at prevTx to find out script-type

    std::vector<uint8_t> pubkey;
    Tx::Iterator iter(tx);
    while (iter.next() != Tx::End) {
        if (iter.tag() == Tx::PrevTxHash) {
            if (iter.uint256Data() == m_prevTxId) {
                iter.next();
                assert(iter.tag() == Tx::PrevTxIndex);
                if (iter.intData() == m_prevOutIndex) {
                    iter.next();
                    assert(iter.tag() == Tx::TxInScript);
                    // Found the input script we need!

                    CScript inScript = iter.byteData();
                    auto scriptIter = inScript.begin();
                    opcodetype type;
                    inScript.GetOp(scriptIter, type); // P2PKH: first signature
                    inScript.GetOp(scriptIter, type, pubkey); // then pubkey
                    break;
                }
            }
        }
        else if (iter.tag() == Tx::OutputValue) { // end of inputs
            break;
        }
    }
    assert(!pubkey.empty());
    if (pubkey.empty()) {
        logFatal() << "DoubleSpendProof; pubkey is empty..."
                   << createHash() << "prevTxId:" << prevTx.createHash() << "|" << m_prevOutIndex;

        try {
            std::string filename = "/data/tx-" + m_prevTxId.ToString();
            std::ofstream out(filename);
            out.write(prevTx.data().begin(), prevTx.size());
        } catch (std::exception &e) {
            logFatal() << "DSP tx-save to disk failed" << e;
        }

        try {
            std::string filename = "/data/dsp-" + createHash().ToString();
            std::ofstream out(filename);
            out << this;
        } catch (std::exception &e) {
            logFatal() << "DSP save to disk failed" << e;
        }

        return Invalid;
    }

    CScript inScript;
    if (scriptType == P2PKH) {
        inScript << m_spender1.pushData.front();
        inScript << pubkey;
    }
    DSPSignatureChecker checker1(this, m_spender1, amount);
    Script::State state(SCRIPT_ENABLE_SIGHASH_FORKID); // we depend on this way of signing
    if (!Script::verify(inScript, prevOutScript, checker1, state)) {
        logDebug(Log::Bitcoin) << "DoubleSpendProof failed validating first tx due to" << state.errorString();
        return Invalid;
    }

    inScript.clear();
    if (scriptType == P2PKH) {
        inScript << m_spender2.pushData.front();
        inScript << pubkey;
    }
    DSPSignatureChecker checker2(this, m_spender2, amount);
    if (!Script::verify(inScript, prevOutScript, checker2, state)) {
        logDebug(Log::Bitcoin) << "DoubleSpendProof failed validating second tx due to" << state.errorString();
        return Invalid;
    }
    return Valid;
}

uint256 DoubleSpendProof::prevTxId() const
{
    return m_prevTxId;
}

int DoubleSpendProof::prevOutIndex() const
{
    return m_prevOutIndex;
}

uint256 DoubleSpendProof::createHash() const
{
    return SerializeHash(*this);
}

void DoubleSpendProof::checkSanityOrThrow() const
{
    if (isEmpty())
        throw std::runtime_error("DSProof is empty");

    // Check limits for both pushData vectors above
    for (auto *pushData : {&m_spender1.pushData, &m_spender2.pushData}) {
        // Message must contain exactly 1 pushData
        if (pushData->size() != 1)
            throw std::runtime_error("DSProof must contain exactly 1 pushData");
        // Script data must be within size limits (520 bytes)
        if (!pushData->empty() && pushData->front().size() > MaxPushDataSize)
            throw std::runtime_error("DSProof script size limit exceeded");
    }
    if (m_spender1.pushData.front() == m_spender2.pushData.front())
        throw std::runtime_error("DSProof noticed both sides are the same");
}
