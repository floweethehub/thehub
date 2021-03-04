/*
 * This file is part of the flowee project
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

#include "Engine.h"
#include "../DoubleSpendProofStorage.h"
#include "../DoubleSpendProof.h"
#include <SettingsDefaults.h>
#include <primitives/transaction.h>
#include <UnspentOutputData.h>
#include "ValidationException.h"
#include "TxValidation_p.h"
#include <Application.h>
#include <main.h>
#include <txorphancache.h>
#include <policy/policy.h>
#include <validationinterface.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <utxo/UnspentOutputDatabase.h>
#include <util.h>
#include <script/sigcache.h>

// #define DEBUG_TRANSACTION_VALIDATION
#ifdef DEBUG_TRANSACTION_VALIDATION
# define DEBUGTX logCritical(Log::TxValidation)
#else
# define DEBUGTX BCH_NO_DEBUG_MACRO()
#endif

using Validation::Exception;

void ValidationPrivate::validateTransactionInputs(CTransaction &tx, const std::vector<UnspentOutput> &unspents, int blockHeight, ValidationFlags flags, int64_t &fees, uint32_t &txSigChecks, bool &spendsCoinbase, bool requireStandard)
{
    assert(unspents.size() == tx.vin.size());
    txSigChecks = 0;

    int64_t valueIn = 0;
    for (size_t i = 0; i < tx.vin.size(); ++i) {
        const ValidationPrivate::UnspentOutput &prevout = unspents.at(i);
        assert(prevout.amount >= 0);
        valueIn += prevout.amount;
    }

    if (valueIn < tx.GetValueOut())
        throw Exception("bad-txns-in-belowout");
    if (!MoneyRange(valueIn)) // Check for negative or overflow input values
        throw Exception("bad-txns-inputvalues-outofrange");
    fees = valueIn - tx.GetValueOut();
    if (fees < 0)
        throw Exception("bad-txns-fee-negative");
    if (!MoneyRange(fees))
        throw Exception("bad-txns-fee-outofrange");

    spendsCoinbase = false;
    const uint32_t scriptValidationFlags = flags.scriptValidationFlags(requireStandard);
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const ValidationPrivate::UnspentOutput &prevout = unspents.at(i);
        if (prevout.isCoinbase) { // If prev is coinbase, check that it's matured
            spendsCoinbase = true;
            if (blockHeight - prevout.blockheight < COINBASE_MATURITY)
                throw Exception("bad-txns-premature-spend-of-coinbase");
        }

        if (!MoneyRange(prevout.amount))
            throw Exception("bad-txns-inputvalues-outofrange");

        // Verify signature
        Script::State strict(scriptValidationFlags);
        if (!Script::verify(tx.vin[i].scriptSig, prevout.outputScript,
                            CachingTransactionSignatureChecker(&tx, i, prevout.amount, true), strict)) {
            // Failures of other flags indicate a transaction that is
            // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
            // such nodes as they are not following the protocol. That
            // said during an upgrade careful thought should be taken
            // as to the correct behavior - we may want to continue
            // peering with non-upgraded nodes even after a soft-fork
            // super-majority vote has passed.

            if (scriptValidationFlags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                // Check whether the failure was caused by a
                // non-mandatory script verification check, such as
                // non-standard DER encodings or non-null dummy
                // arguments; if so, don't trigger DoS protection to
                // avoid splitting the network between upgraded and
                // non-upgraded nodes.
                Script::State flexible(scriptValidationFlags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS);
                if (Script::verify(tx.vin[i].scriptSig, prevout.outputScript, TransactionSignatureChecker(&tx, i, prevout.amount), flexible))
                    throw Exception(strprintf("non-mandatory-script-verify-flag (%s)", strict.errorString()), Validation::RejectNonstandard, 0);
            }

            throw Exception(strprintf("mandatory-script-verify-flag-failed (%s)", strict.errorString()));
        }
        txSigChecks += strict.sigCheckCount;
    }
}


// static
void Validation::checkTransaction(const CTransaction &tx)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        throw Exception("bad-txns-vin-empty", 10);
    if (tx.vout.empty())
        throw Exception("bad-txns-vout-empty", 10);
    // Size limits
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_TX_SIZE)
        throw Exception("bad-txns-oversize", 100);

    // Check for negative or overflow output values
    int64_t nValueOut = 0;
    for (const CTxOut& txout : tx.vout) {
        if (txout.nValue < 0)
            throw Exception("bad-txns-vout-negative", 100);
        if (txout.nValue > MAX_MONEY)
            throw Exception("bad-txns-vout-toolarge", 100);
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            throw Exception("bad-txns-txouttotal-toolarge", 100);
    }

    // Check for duplicate inputs
    std::set<COutPoint> vInOutPoints;
    for (const CTxIn& txin : tx.vin) {
        if (vInOutPoints.count(txin.prevout))
            throw Exception("bad-txns-inputs-duplicate", 100);
        vInOutPoints.insert(txin.prevout);
    }

    if (tx.IsCoinBase()) {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            throw Exception("bad-cb-length", 100);
    } else {
        for (const CTxIn& txin : tx.vin) {
            if (txin.prevout.IsNull())
                throw Exception("bad-txns-prevout-null", 10);
        }
    }
}


TxValidationState::TxValidationState(const std::weak_ptr<ValidationEnginePrivate> &parent, const Tx &transaction, uint32_t onValidationFlags)
    : m_parent(parent),
      m_tx(transaction),
      m_validationFlags(onValidationFlags),
      m_originatingNodeId(-1),
      m_originalInsertTime(0)
{
}

TxValidationState::~TxValidationState()
{
    try {
        m_promise.set_value(std::string());
    } catch (std::exception &) {}
}

void TxValidationState::checkTransaction()
{
    std::shared_ptr<ValidationEnginePrivate> parent = m_parent.lock();
    if (parent.get() == nullptr)
        return;
    const ValidationFlags flags = parent->tipFlags;
    std::string result;
    struct RAII {
        RAII(std::promise<std::string> *promise) : promise(promise) {}
        ~RAII() {
            promise->set_value(result);
        }
        std::promise<std::string> *promise;
        std::string result;
    };
    RAII raii(&m_promise);

    if (flags.hf201811Active && m_tx.size() < 100)
        throw Exception("bad-txns-undersize", 2);

    const uint256 txid = m_tx.createHash();
    DEBUGTX << "checkTransaction peer:" << m_originatingNodeId << txid;
    auto tx = m_tx.createOldTransaction();
    bool inputsMissing = false;
    try {
        Validation::checkTransaction(tx);

        // Coinbase is only valid in a block, not as a loose transaction
        if (tx.IsCoinBase())
            throw Exception("coinbase", 100);

        // Rather not work on nonstandard transactions (unless -testnet/-testnet4/-scalenet/-regtest)
        std::string reason;
        if (fRequireStandard && !IsStandardTx(tx, reason))
            throw Exception(reason, Validation::RejectNonstandard, 0);

        // Don't relay version 2 transactions until CSV is active, and we can be
        // sure that such transactions will be mined (unless we're on
        // -testnet/-testnet4/-scalenet/-regtest).
        if (fRequireStandard && tx.nVersion >= 2 &&  flags.nLocktimeVerifySequence == false)
            throw Exception("premature-version2-tx", Validation::RejectNonstandard, 0);
        // Only accept nLockTime-using transactions that can be mined in the next
        // block; we don't want our mempool filled up with transactions that can't
        // be mined yet.
        CBlockIndex *tip = parent->tip.load();
        if (tip == nullptr) // don't accept anything before we have a genesis block.
            return;
        if (!IsFinalTx(tx, tip->nHeight + 1, tip->GetMedianTimePast()))
            throw Exception("non-final", Validation::RejectNonstandard, 0);

        CTxMemPoolEntry entry(m_tx);
        entry.entryHeight = static_cast<std::uint32_t>(tip->nHeight);
        entry.inChainInputValue = 0;

        {
            /*
             * Now we iterate over the inputs of the tx and connect them to outputs they spend.
             * We reject when something is fishy.
             *
             * Outputs they spend can come from the mempool or the UTXO. We have a codepath to address each separately.
             * This optimizes for speed, so we don't try to push both usecases though the same codepath. This may mean
             * its a tad harder to follow.  I am only sorry for not being sorry.
             */

            std::vector<Tx> mempoolTransactions;
            mempoolTransactions.resize(tx.vin.size());
            {
                LOCK(parent->mempool->cs);
                // do we already have the input tx?
                if (parent->mempool->exists(txid))
                    throw Exception("txn-already-known", Validation::RejectAlreadyKnown, 0);

                // find the ones in the mempool
                for (size_t i = 0; i < tx.vin.size(); ++i) {
                    Tx prevTx;
                    if (parent->mempool->lookup(tx.vin[i].prevout.hash, prevTx))
                        mempoolTransactions[i] = prevTx;
                }
            }

            std::vector<ValidationPrivate::UnspentOutput> unspents; // list of outputs
            unspents.resize(tx.vin.size());
            double txPriority = 0;
            for (size_t i = 0; i < tx.vin.size(); ++i) {
                ValidationPrivate::UnspentOutput &prevOut = unspents[i];
                if (mempoolTransactions.at(i).isValid()) { // we found it in the mempool above, in the mempool->lock!
                    // check if the referenced output exists
                    // we do that here, outside of the mempool lock, so we don't have to do that later.
                    Tx::Iterator iter(mempoolTransactions.at(i));
                    uint32_t outputs = 0;
                    const uint32_t prevoutIndex = tx.vin.at(i).prevout.n;
                    while (iter.next(Tx::OutputValue) != Tx::End) { // find all output-value tags.
                        if (outputs++ == prevoutIndex)
                            break;
                    }
                    if (outputs - 1 < prevoutIndex) {
                        throw Exception("missing-inputs", 10); // we have a tx it is trying to spend, but the input doesn't exist.
                    }
                    prevOut.amount = static_cast<int64_t>(iter.longData());
                    auto type = iter.next();
                    assert(type == Tx::OutputScript); // if it made it into the mempool, its supposed to be well formed.
                    prevOut.outputScript = iter.byteData();
                    if (fRequireStandard) {
                        // Check for non-standard pay-to-script-hash in inputs
                        if (!Policy::isInputStandard(prevOut.outputScript, tx.vin.at(i).scriptSig))
                            throw Exception("bad-txns-nonstandard-inputs", Validation::RejectNonstandard, 0);
                    }
                }
                else {
                    // prevOut not in mempool, check UTXO
                    assert(tx.vin[i].prevout.n < 0xEFFFFFFF); // utxo db would not like that. 'n' should not get even moderately big, though.
                    UnspentOutputData data(g_utxo->find(tx.vin[i].prevout.hash, static_cast<int>(tx.vin[i].prevout.n)));
                    if (!data.isValid()) {
                        inputsMissing = true;
                        DEBUGTX << "The output we are trying to spend is unknown to us" << tx.vin[i].prevout.hash << "Me:" << txid;
                        throw Exception("missing-inputs", 0);
                    }
                    prevOut.amount = data.outputValue();
                    prevOut.outputScript = data.outputScript();
                    prevOut.isCoinbase = data.isCoinbase();
                    prevOut.blockheight = data.blockHeight();
                    if (fRequireStandard) {
                        // Check for non-standard pay-to-script-hash in inputs
                        if (!Policy::isInputStandard(prevOut.outputScript, tx.vin.at(i).scriptSig))
                            throw Exception("bad-txns-nonstandard-inputs", Validation::RejectNonstandard, 0);
                    }
                    txPriority += prevOut.amount * (entry.entryHeight - data.blockHeight());
                }
                entry.inChainInputValue += prevOut.amount;
            }


            // Only accept BIP68 sequence locked transactions that can be mined in the next
            // block; we don't want our mempool filled up with transactions that can't
            // be mined yet.
            if (!CheckSequenceLocks(*parent->mempool, tx, STANDARD_LOCKTIME_VERIFY_FLAGS, &entry.lockPoints, false, tip))
                throw Exception("non-BIP68-final", Validation::RejectNonstandard, 0);

            uint32_t txSigChecks = 0;
            ValidationPrivate::validateTransactionInputs(tx, unspents, static_cast<int>(entry.entryHeight) + 1, flags, entry.nFee, txSigChecks , entry.spendsCoinbase, fRequireStandard);
            if (fRequireStandard && txSigChecks > Policy::MAX_SIGCHEKCS_PER_TX) {
                throw Exception("bad-blk-sigcheck", Validation::RejectNonstandard, 0);
            }

            // nModifiedFees includes any fee deltas from PrioritiseTransaction
            int64_t nModifiedFees = entry.nFee;
            double nPriorityDummy = 0;
            parent->mempool->ApplyDeltas(txid, nPriorityDummy, nModifiedFees);
            entry.entryPriority = entry.oldTx.ComputePriority(txPriority, entry.tx.size());
            entry.hadNoDependencies = parent->mempool->HasNoInputsOf(tx);

            const size_t nSize = entry.GetTxSize();

            int64_t mempoolRejectFee = parent->mempool->GetMinFee().GetFee(nSize);
            if (mempoolRejectFee > 0 && nModifiedFees < mempoolRejectFee) {
                logInfo(Log::Mempool) << "transaction rejected, low fee:" << nModifiedFees << "<" << mempoolRejectFee << "sat";
                throw Exception("mempool min fee not met", Validation::RejectInsufficientFee, 0);
            } else if (GetBoolArg("-relaypriority", Settings::DefaultRelayPriority) && nModifiedFees < ::minRelayTxFee.GetFee(nSize)
                       && !AllowFree(entry.GetPriority(static_cast<uint32_t>(tip->nHeight + 1)))) {
                // Require that free transactions have sufficient priority to be mined in the next block.
                raii.result = std::string("insufficient priority");
                return;
            }

            // Continuously rate-limit free and very-low-fee transactions
            // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
            // be annoying or make others' transactions take longer to confirm.
            if ((m_validationFlags & Validation::RateLimitFreeTx) && nModifiedFees < ::minRelayTxFee.GetFee(nSize)) {
                static CCriticalSection csFreeLimiter;
                static double dFreeCount;
                static int64_t nLastTime;
                int64_t nNow = GetTime();

                LOCK(csFreeLimiter);

                // Use an exponentially decaying ~10-minute window:
                dFreeCount *= pow(1.0 - 1.0/600.0, static_cast<double>(nNow - nLastTime));
                nLastTime = nNow;
                // -limitfreerelay unit is thousand-bytes-per-minute
                // At default rate it would take over a month to fill 1GB
                if (dFreeCount >= GetArg("-limitfreerelay", Settings::DefaultLimitFreeRelay) * 10 * 1000)
                    throw Exception("rate limited free transaction", Validation::RejectInsufficientFee, 0);
                logInfo(Log::TxValidation) << "Rate limit dFreeCount:" << dFreeCount << "=>" << dFreeCount + nSize;
                dFreeCount += nSize;
            }

            if ((m_validationFlags & Validation::RejectAbsurdFeeTx) && entry.nFee > ::minRelayTxFee.GetFee(nSize) * 10000)
                throw Exception("absurdly-high-fee", 0);

            // Calculate in-mempool ancestors, up to a limit.
            CTxMemPool::setEntries setAncestors;
            int64_t nLimitAncestors = GetArg("-limitancestorcount", Settings::DefaultAncestorLimit);
            int64_t nLimitAncestorSize = GetArg("-limitancestorsize", Settings::DefaultAncestorSizeLimit)*1000;
            int64_t nLimitDescendants = GetArg("-limitdescendantcount", Settings::DefaultDescendantLimit);
            int64_t nLimitDescendantSize = GetArg("-limitdescendantsize", Settings::DefaultDescendantSizeLimit)*1000;
            std::string errString;
            if (!parent->mempool->CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize,
                                                            nLimitDescendants, nLimitDescendantSize, errString)) {
                logInfo(Log::TxValidation) << "Tx rejected from mempool (too-long-mempool-chain). Reason:" << errString;
                throw Exception("too-long-mempool-chain", Validation::RejectNonstandard, 0);
            }

            if (!parent->mempool->insertTx(entry)) {
                raii.result = "bad-txns-inputs-spent";
                DEBUGTX << "Mempool did not accept tx entry, returned false";
                return;
            }
            if (entry.dsproof != -1) {
                m_doubleSpendTx = entry.tx;
                m_doubleSpendProofId = entry.dsproof;

                parent->strand.post(std::bind(&TxValidationState::notifyDoubleSpend, shared_from_this()));
            }
        }

        logDebug(Log::TxValidation) << "accepted:"<< txid << "peer:" << m_originatingNodeId
                               << "(poolsz" << parent->mempool->size() << "txn," << (parent->mempool->DynamicMemoryUsage() / 1000) << "kB)";


        if (m_validationFlags & FromMempool) {
            // AcceptToMemoryPool/addUnchecked all assume that new mempool entries have
            // no in-mempool children, which is generally not true when adding
            // previously-confirmed transactions back to the mempool.
            // UpdateTransactionsFromBlock finds descendants of any transactions in this
            // block that were added back and cleans up the mempool state.
            std::vector<uint256> me;
            me.push_back(txid);
            parent->mempool->UpdateTransactionsFromBlock(me);
        }

        if ((m_validationFlags & Validation::TxValidateOnly) == 0)
            RelayTransaction(tx);

        auto orphans = CTxOrphanCache::instance()->fetchTransactionsByPrev(txid);
        std::vector<uint256> scheduled;
        scheduled.reserve(orphans.size());
        for (auto orphan : orphans) {
            std::shared_ptr<TxValidationState> state(new TxValidationState(m_parent, Tx::fromOldTransaction(orphan.tx), orphan.onResultFlags));
            state->m_originatingNodeId = orphan.fromPeer;
            state->m_originalInsertTime = orphan.nEntryTime;
            scheduled.push_back(state->m_tx.createHash());
            Application::instance()->ioService().post(std::bind(&TxValidationState::checkTransaction, state));
        }

        CTxOrphanCache::instance()->eraseOrphans(scheduled);
        CTxOrphanCache::instance()->eraseOrphansByTime();

        parent->strand.post(std::bind(&TxValidationState::sync, shared_from_this()));
    } catch (const Validation::DoubleSpendException &ex) {
        raii.result = strprintf("%i: %s", Validation::RejectConflict, "txn-mempool-conflict");
        if (ex.id != -1) // to avoid log file confusion, don't mention this for anything but the first DS
            logWarning(Log::TxValidation) << "Tx-Validation found a double spend";

        if ((m_validationFlags & Validation::TxValidateOnly) == 0) {
            m_doubleSpendTx = ex.otherTx;
            m_doubleSpendProofId = ex.id;
            parent->strand.post(std::bind(&TxValidationState::notifyDoubleSpend, shared_from_this()));

            std::lock_guard<std::mutex> rejects(parent->recentRejectsLock);
            parent->recentTxRejects.insert(txid);
        }
    } catch (const Exception &ex) {
        raii.result = strprintf("%i: %s", ex.rejectCode(), ex.what());
        if (inputsMissing) {// if missing inputs, add to orphan cache
            DEBUGTX << "Tx missed inputs, can't add to mempool" << txid;
            if ((m_validationFlags & Validation::TxValidateOnly) || m_originatingNodeId < 0)
                return;
            CTxOrphanCache *cache = CTxOrphanCache::instance();
            // DoS prevention: do not allow CTxOrphanCache to grow unbounded
            cache->addOrphanTx(tx, m_originatingNodeId, m_validationFlags, m_originalInsertTime);
            std::uint32_t nEvicted = cache->limitOrphanTxSize();
            if (nEvicted > 0)
                logDebug(Log::TxValidation) << "mapOrphan overflow, removed" << nEvicted << "tx";
        }
        logInfo(Log::TxValidation) << "Tx-Validation failed" << ex << "peer:" << m_originatingNodeId;

        if (ex.punishment() > 0 && (m_validationFlags & Validation::PunishBadNode)) {
            assert(m_originatingNodeId >= 0);
            LOCK(cs_main);
            CNode *node = FindNode(m_originatingNodeId);
            if (node) {
                node->PushMessage(NetMsgType::REJECT, std::string(NetMsgType::TX),
                                      static_cast<uint8_t>(ex.rejectCode()),
                                      std::string(ex.what()).substr(0, MAX_REJECT_MESSAGE_LENGTH), txid);
               if (ex.punishment() > 0)
                   Misbehaving(m_originatingNodeId, ex.punishment());
            }
        }

        std::lock_guard<std::mutex> rejects(parent->recentRejectsLock);
        parent->recentTxRejects.insert(txid);
    } catch (const std::runtime_error &ex) {
        raii.result = std::string(ex.what());
        logFatal(Log::TxValidation) << "TxValidation" << txid << "got exception:" << ex;
        logFatal(Log::TxValidation) << "  size" << m_tx.size() << m_tx.createHash();
        assert(false);
    }
}

void TxValidationState::sync()
{
    std::shared_ptr<ValidationEnginePrivate> parent = m_parent.lock();
    if (parent.get() == nullptr)
        return;
    assert(parent->strand.running_in_this_thread());

    LimitMempoolSize(*parent->mempool, GetArg("-maxmempool", Settings::DefaultMaxMempoolSize) * 1000000,
                     GetArg("-mempoolexpiry", Settings::DefaultMempoolExpiry) * 60 * 60);

    ValidationNotifier().syncTransaction(m_tx.createOldTransaction());
    ValidationNotifier().syncTx(m_tx);
}

void TxValidationState::notifyDoubleSpend()
{
    std::shared_ptr<ValidationEnginePrivate> parent = m_parent.lock();
    if (parent.get() == nullptr)
        return;
    assert(parent->strand.running_in_this_thread());

    // send INV to all peers
    if (m_doubleSpendProofId != -1) {
        auto dsp = mempool.doubleSpendProofStorage()->proof(m_doubleSpendProofId);
        if (!dsp.isEmpty()) {
            CInv inv(MSG_DOUBLESPENDPROOF, dsp.createHash());
            const CTransaction dspTx = m_doubleSpendTx.createOldTransaction();
            logDebug(Log::DSProof) << "Broadcasting DSP" << inv;

            LOCK(cs_vNodes);
            for (CNode* pnode : vNodes) {
                if (!pnode->fRelayTxes)
                    continue;
                LOCK(pnode->cs_filter);
                if (pnode->pfilter) {
                    // For nodes that we sent this Tx before, send a proof.
                    if (pnode->pfilter->isRelevantAndUpdate(dspTx)) {
                        logDebug(Log::DSProof) << "  peer:" << pnode->id;
                        pnode->PushInventory(inv);
                    }
                } else {
                    logDebug(Log::DSProof) << "  peer:" << pnode->id;
                    pnode->PushInventory(inv);
                }
            }
        }
    }

    ValidationNotifier().doubleSpendFound(m_doubleSpendTx, m_tx);
}
