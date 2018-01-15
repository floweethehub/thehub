/*
 * This file is part of the flowee project
 * Copyright (C) 2017-2018 Tom Zander <tomz@freedommail.ch>
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
#include "ValidationException.h"
#include "TxValidation_p.h"
#include <Application.h>
#include <main.h>
#include <txorphancache.h>
#include <policy/policy.h>
#include <validationinterface.h>
#include <chainparams.h>
#include <consensus/consensus.h>

// #define DEBUG_TRANSACTION_VALIDATION
#ifdef DEBUG_TRANSACTION_VALIDATION
# define DEBUGTX logCritical(Log::TxValidation)
#else
# define DEBUGTX BTC_NO_DEBUG_MACRO()
#endif

using Validation::Exception;

void ValidationPrivate::validateTransactionInputs(CTransaction &tx, const std::vector<CCoins> &coins, int blockHeight, ValidationFlags flags, int64_t &fees, uint32_t &txSigops, bool &spendsCoinbase)
{
    assert(coins.size() == tx.vin.size());

    int64_t valueIn = 0;
    assert(tx.vin.size() == coins.size());
    txSigops = 0;
    for (size_t i = 0; i < tx.vin.size(); ++i) {
        const CTxOut &prevout = coins.at(i).vout[tx.vin[i].prevout.n];
        if (flags.strictPayToScriptHash && prevout.scriptPubKey.IsPayToScriptHash()) {
            // Add in sigops done by pay-to-script-hash inputs;
            // this is to prevent a "rogue miner" from creating
            // an incredibly-expensive-to-validate block.
            txSigops += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
        }
        valueIn += prevout.nValue;
    }
    if (txSigops > MAX_BLOCK_SIGOPS_PER_MB)
        throw Exception("bad-tx-sigops");

    if (valueIn < tx.GetValueOut())
        throw Exception("bad-txns-in-belowout");
    if (!MoneyRange(valueIn)) // Check for negative or overflow input values
        throw Exception("bad-txns-inputvalues-outofrange");
    fees = valueIn - tx.GetValueOut();
    if (fees < 0)
        throw Exception("bad-txns-fee-negative");
    if (!MoneyRange(fees))
        throw Exception("bad-txns-fee-outofrange");


    if (flags.uahfRules) {
        // reject in memory pool transactions that use the OP_RETURN anti-replay ID.
        // Remove this code after the sunset height has been reached.
        const auto consensusParams = Params().GetConsensus();
        if (blockHeight <= consensusParams.antiReplayOpReturnSunsetHeight) {
            for (const CTxOut &o : tx.vout) {
                if (o.scriptPubKey.isCommitment(consensusParams.antiReplayOpReturnCommitment))
                    throw Exception("auti-replay-opreturn-commitment");
            }
        }
    }

    spendsCoinbase = false;
    const uint32_t scriptValidationFlags = flags.scriptValidationFlags();
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CCoins &coin = coins.at(i);
        if (coin.IsCoinBase()) { // If prev is coinbase, check that it's matured
            spendsCoinbase = true;
            if (blockHeight - coin.nHeight < COINBASE_MATURITY)
                throw Exception("bad-txns-premature-spend-of-coinbase");
        }

        if (!MoneyRange(coin.vout[tx.vin.at(i).prevout.n].nValue))
            throw Exception("bad-txns-inputvalues-outofrange");

        // Verify signature
        CScriptCheck check(coin, tx, i, scriptValidationFlags, false);
        if (!check()) {
            if (scriptValidationFlags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                // Check whether the failure was caused by a
                // non-mandatory script verification check, such as
                // non-standard DER encodings or non-null dummy
                // arguments; if so, don't trigger DoS protection to
                // avoid splitting the network between upgraded and
                // non-upgraded nodes.
                CScriptCheck check2(coin, tx, i, scriptValidationFlags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, false);
                if (check2())
                    throw Exception(strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())), Validation::RejectNonstandard);
            }
            // Failures of other flags indicate a transaction that is
            // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
            // such nodes as they are not following the protocol. That
            // said during an upgrade careful thought should be taken
            // as to the correct behavior - we may want to continue
            // peering with non-upgraded nodes even after a soft-fork
            // super-majority vote has passed.
            throw Exception(strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(check.GetScriptError())));
        }
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
    CAmount nValueOut = 0;
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
    } catch (std::exception &e) {}
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

    const uint256 txid = m_tx.createHash();
    DEBUGTX << "checkTransaction peer:" << m_originatingNodeId << txid;
    auto tx = m_tx.createOldTransaction();
    bool inputsMissing = false;
    try {
        Validation::checkTransaction(tx);

        // Coinbase is only valid in a block, not as a loose transaction
        if (tx.IsCoinBase())
            throw Exception("coinbase", 100);

        // Rather not work on nonstandard transactions (unless -testnet/-regtest)
        std::string reason;
        if (fRequireStandard && !IsStandardTx(tx, reason))
            throw Exception(reason, Validation::RejectNonstandard, 0);

        // Don't relay version 2 transactions until CSV is active, and we can be
        // sure that such transactions will be mined (unless we're on
        // -testnet/-regtest).
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
        entry.entryHeight = tip->nHeight;

        {
            CCoinsView dummy;
            CCoinsViewCache view(&dummy);

            std::vector<CCoins> coins;
            {
                LOCK(parent->mempool->cs);
                CCoinsViewMemPool viewMemPool(parent->mempool);
                view.SetBackend(viewMemPool);

                // do we already have it?
                if (view.HaveCoins(txid))
                    throw Exception("txn-already-known", Validation::RejectAlreadyKnown, 0);

                // do all inputs exist?
                // Note that this does not check for the presence of actual outputs (see the next check for that),
                // and only helps with filling in inputsMissing (to determine missing vs spent).
                for (const CTxIn txin : tx.vin) {
                    if (!view.HaveCoins(txin.prevout.hash)) {
                        inputsMissing = true;
                        throw Exception("missing-inputs", 0);
                    }
                }

                // are the actual inputs available?
                if (!view.HaveInputs(tx))
                    throw Exception("bad-txns-inputs-spent", Validation::RejectDuplicate, 0);

                // Bring the best block into scope
                view.GetBestBlock();

                // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
                view.SetBackend(dummy);

                // Only accept BIP68 sequence locked transactions that can be mined in the next
                // block; we don't want our mempool filled up with transactions that can't
                // be mined yet.
                // Must keep pool.cs for this unless we change CheckSequenceLocks to take a
                // CoinsViewCache instead of create its own
                if (!CheckSequenceLocks(*parent->mempool, tx, STANDARD_LOCKTIME_VERIFY_FLAGS, &entry.lockPoints, false, tip))
                    throw Exception("non-BIP68-final", Validation::RejectNonstandard, 0);

                coins = view.coinsForTransaction(m_tx);
            }

            // Check for non-standard pay-to-script-hash in inputs
            if (fRequireStandard && !Policy::areInputsStandard(tx, coins))
                throw Exception("bad-txns-nonstandard-inputs", Validation::RejectNonstandard, 0);

            ValidationPrivate::validateTransactionInputs(tx, coins, entry.entryHeight + 1, flags, entry.nFee, entry.sigOpCount, entry.spendsCoinbase);
            coins.clear();

            // nModifiedFees includes any fee deltas from PrioritiseTransaction
            CAmount nModifiedFees = entry.nFee;
            double nPriorityDummy = 0;
            parent->mempool->ApplyDeltas(txid, nPriorityDummy, nModifiedFees);

            entry.entryPriority = view.GetPriority(tx, entry.entryHeight, entry.inChainInputValue);
            entry.hadNoDependencies = parent->mempool->HasNoInputsOf(tx);

            const unsigned int nSize = entry.GetTxSize();

            // Notice nBytesPerSigOp is a global!
            if ((entry.sigOpCount > MAX_STANDARD_TX_SIGOPS) || (nBytesPerSigOp && entry.sigOpCount > nSize / nBytesPerSigOp))
                throw Exception("bad-txns-too-many-sigops", Validation::RejectNonstandard);

            CAmount mempoolRejectFee = parent->mempool->GetMinFee(GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFee(nSize);
            if (mempoolRejectFee > 0 && nModifiedFees < mempoolRejectFee) {
                // return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool min fee not met", false, strprintf("%d < %d", nFees, mempoolRejectFee));
                throw Exception("mempool min fee not met", Validation::RejectInsufficientFee, 0);
            } else if (GetBoolArg("-relaypriority", DEFAULT_RELAYPRIORITY) && nModifiedFees < ::minRelayTxFee.GetFee(nSize)
                       && !AllowFree(entry.GetPriority(tip->nHeight + 1))) {
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
                dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
                nLastTime = nNow;
                // -limitfreerelay unit is thousand-bytes-per-minute
                // At default rate it would take over a month to fill 1GB
                if (dFreeCount >= GetArg("-limitfreerelay", DEFAULT_LIMITFREERELAY) * 10 * 1000)
                    throw Exception("rate limited free transaction", Validation::RejectInsufficientFee, 0);
                logInfo(Log::TxValidation) << "Rate limit dFreeCount:" << dFreeCount << "=>" << dFreeCount + nSize;
                dFreeCount += nSize;
            }

            if ((m_validationFlags & Validation::RejectAbsurdFeeTx) && entry.nFee > ::minRelayTxFee.GetFee(nSize) * 10000)
                throw Exception("absurdly-high-fee", 0);

            // Calculate in-mempool ancestors, up to a limit.
            CTxMemPool::setEntries setAncestors;
            int64_t nLimitAncestors = GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
            int64_t nLimitAncestorSize = GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT)*1000;
            int64_t nLimitDescendants = GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
            int64_t nLimitDescendantSize = GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT)*1000;
            std::string errString;
            if (!parent->mempool->CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize,
                                                            nLimitDescendants, nLimitDescendantSize, errString)) {
                throw Exception("too-long-mempool-chain", Validation::RejectNonstandard, 0);
            }

            if (!parent->mempool->insertTx(entry)) {
                raii.result = "bad-txns-inputs-spent";
                DEBUGTX << "Mempool did not accept tx entry, returned false";
                return;
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

        parent->mempool->check();
        if (m_validationFlags & Validation::ForwardGoodToPeers)
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

        CTxOrphanCache::instance()->EraseOrphans(scheduled);
        CTxOrphanCache::instance()->EraseOrphansByTime();

        parent->strand.post(std::bind(&TxValidationState::sync, shared_from_this()));
    } catch (const Exception &ex) {
        raii.result = strprintf("%i: %s", ex.rejectCode(), ex.what());
        if (inputsMissing) {// if missing inputs, add to orphan cache
            DEBUGTX << "Tx missed inputs, can't add to mempool" << txid;
            if ((m_validationFlags & FromMempool) == 0 && m_originatingNodeId < 0)
                return;
            CTxOrphanCache *cache = CTxOrphanCache::instance();
            // DoS prevention: do not allow CTxOrphanCache to grow unbounded
            cache->AddOrphanTx(tx, m_originatingNodeId, m_validationFlags, m_originalInsertTime);
            std::uint32_t nEvicted = cache->LimitOrphanTxSize();
            if (nEvicted > 0)
                logDebug(Log::TxValidation) << "mapOrphan overflow, removed" << nEvicted << "tx";
        }
        logWarning(Log::TxValidation) << "Tx-Validation failed" << ex << "peer:" << m_originatingNodeId;

        if (ex.punishment() > 0 && (m_validationFlags & Validation::PunishBadNode)) {
            assert(m_originatingNodeId >= 0);
            LOCK(cs_main);
            CNode *node = FindNode(m_originatingNodeId);
            if (node) {
                node->PushMessage(NetMsgType::REJECT, std::string(NetMsgType::TX), (uint8_t)ex.rejectCode(),
                                      std::string(ex.what()).substr(0, MAX_REJECT_MESSAGE_LENGTH), txid);
               if (ex.punishment() > 0)
                   Misbehaving(m_originatingNodeId, ex.punishment());
            }
        }

        std::lock_guard<std::mutex> rejects(parent->recentRejectsLock);
        parent->recentTxRejects.insert(txid);
    } catch (const std::runtime_error &ex) {
        raii.result = std::string(ex.what());
        logDebug() << ex;
        assert(false);
    }
}

void TxValidationState::sync()
{
    std::shared_ptr<ValidationEnginePrivate> parent = m_parent.lock();
    if (parent.get() == nullptr)
        return;
    assert(parent->strand.running_in_this_thread());

    LimitMempoolSize(*parent->mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
                     GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);

    SyncWithWallets(m_tx.createOldTransaction(), nullptr);
}

