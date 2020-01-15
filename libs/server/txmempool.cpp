/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2015 The Bitcoin Core developers
 * Copyright (C) 2017-2020 Tom Zander <tomz@freedommail.ch>
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
#include "DoubleSpendProofStorage.h"
#include "txmempool.h"

#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "main.h"
#include "streaming/streams.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utiltime.h"
#include "version.h"
#include <core_memusage.h>

#include <utxo/UnspentOutputDatabase.h>
#include <validation/ValidationException.h>
#include <validationinterface.h>

CTxMemPoolEntry::CTxMemPoolEntry(const Tx &tx)
    : tx(tx),
    nModFeesWithDescendants(0)
{
    oldTx = tx.createOldTransaction();
    nTime = ::GetTime();
    nTxSize = tx.size();
    nModSize = oldTx.CalculateModifiedSize(nTxSize);
    nUsageSize = RecursiveDynamicUsage(oldTx);
    nCountWithDescendants = 1;
    nSizeWithDescendants = nTxSize;

    feeDelta = 0;
}

CTxMemPoolEntry::CTxMemPoolEntry(const CTransaction &tx, const CAmount& _nFee,
                                 int64_t _nTime, double _entryPriority, unsigned int _entryHeight,
                                 bool poolHasNoInputsOf, CAmount _inChainInputValue,
                                 bool _spendsCoinbase, unsigned int _sigOps, LockPoints lp)
    : CTxMemPoolEntry(Tx::fromOldTransaction(tx))
{
    nFee = _nFee;
    nModFeesWithDescendants = nFee;
    nTime = _nTime;
    entryPriority = _entryPriority;
    entryHeight = _entryHeight;
    hadNoDependencies = poolHasNoInputsOf;
    inChainInputValue = _inChainInputValue;
    spendsCoinbase = _spendsCoinbase;
    sigOpCount = _sigOps;
    lockPoints = lp;

    assert(inChainInputValue <= oldTx.GetValueOut() + nFee);
}

CTxMemPoolEntry::CTxMemPoolEntry(const CTxMemPoolEntry& other)
{
    *this = other;
}

double
CTxMemPoolEntry::GetPriority(unsigned int currentHeight) const
{
    double deltaPriority = ((double)(currentHeight-entryHeight)*inChainInputValue)/nModSize;
    double dResult = entryPriority + deltaPriority;
    if (dResult < 0) // This should only happen if it was called with a height below entry height
        dResult = 0;
    return dResult;
}

void CTxMemPoolEntry::UpdateFeeDelta(int64_t newFeeDelta)
{
    nModFeesWithDescendants += newFeeDelta - feeDelta;
    feeDelta = newFeeDelta;
}

void CTxMemPoolEntry::UpdateLockPoints(const LockPoints& lp)
{
    lockPoints = lp;
}

// Update the given tx for any in-mempool descendants.
// Assumes that setMemPoolChildren is correct for the given tx and all
// descendants.
bool CTxMemPool::UpdateForDescendants(txiter updateIt, int maxDescendantsToVisit, cacheMap &cachedDescendants, const std::set<uint256> &setExclude)
{
    // Track the number of entries (outside setExclude) that we'd need to visit
    // (will bail out if it exceeds maxDescendantsToVisit)
    int nChildrenToVisit = 0;

    setEntries stageEntries, setAllDescendants;
    stageEntries = GetMemPoolChildren(updateIt);

    while (!stageEntries.empty()) {
        const txiter cit = *stageEntries.begin();
        if (cit->IsDirty()) {
            // Don't consider any more children if any descendant is dirty
            return false;
        }
        setAllDescendants.insert(cit);
        stageEntries.erase(cit);
        const setEntries &setChildren = GetMemPoolChildren(cit);
        for (const txiter childEntry : setChildren) {
            cacheMap::iterator cacheIt = cachedDescendants.find(childEntry);
            if (cacheIt != cachedDescendants.end()) {
                // We've already calculated this one, just add the entries for this set
                // but don't traverse again.
                for (const txiter cacheEntry : cacheIt->second) {
                    // update visit count only for new child transactions
                    // (outside of setExclude and stageEntries)
                    if (setAllDescendants.insert(cacheEntry).second &&
                            !setExclude.count(cacheEntry->GetTx().GetHash()) &&
                            !stageEntries.count(cacheEntry)) {
                        nChildrenToVisit++;
                    }
                }
            } else if (!setAllDescendants.count(childEntry)) {
                // Schedule for later processing and update our visit count
                if (stageEntries.insert(childEntry).second && !setExclude.count(childEntry->GetTx().GetHash())) {
                        nChildrenToVisit++;
                }
            }
            if (nChildrenToVisit > maxDescendantsToVisit) {
                return false;
            }
        }
    }
    // setAllDescendants now contains all in-mempool descendants of updateIt.
    // Update and add to cached descendant map
    int64_t modifySize = 0;
    CAmount modifyFee = 0;
    int64_t modifyCount = 0;
    for (txiter cit : setAllDescendants) {
        if (!setExclude.count(cit->GetTx().GetHash())) {
            modifySize += cit->GetTxSize();
            modifyFee += cit->GetModifiedFee();
            modifyCount++;
            cachedDescendants[updateIt].insert(cit);
        }
    }
    mapTx.modify(updateIt, update_descendant_state(modifySize, modifyFee, modifyCount));
    return true;
}

// vHashesToUpdate is the set of transaction hashes from a disconnected block
// which has been re-added to the mempool.
// for each entry, look for descendants that are outside hashesToUpdate, and
// add fee/size information for such descendants to the parent.
void CTxMemPool::UpdateTransactionsFromBlock(const std::vector<uint256> &vHashesToUpdate)
{
    LOCK(cs);
    // For each entry in vHashesToUpdate, store the set of in-mempool, but not
    // in-vHashesToUpdate transactions, so that we don't have to recalculate
    // descendants when we come across a previously seen entry.
    cacheMap mapMemPoolDescendantsToUpdate;

    // Use a set for lookups into vHashesToUpdate (these entries are already
    // accounted for in the state of their ancestors)
    std::set<uint256> setAlreadyIncluded(vHashesToUpdate.begin(), vHashesToUpdate.end());

    // Iterate in reverse, so that whenever we are looking at at a transaction
    // we are sure that all in-mempool descendants have already been processed.
    // This maximizes the benefit of the descendant cache and guarantees that
    // setMemPoolChildren will be updated, an assumption made in
    // UpdateForDescendants.
    BOOST_REVERSE_FOREACH(const uint256 &hash, vHashesToUpdate) {
        // we cache the in-mempool children to avoid duplicate updates
        setEntries setChildren;
        // calculate children from mapNextTx
        txiter it = mapTx.find(hash);
        if (it == mapTx.end()) {
            continue;
        }
        std::map<COutPoint, CInPoint>::iterator iter = mapNextTx.lower_bound(COutPoint(hash, 0));
        // First calculate the children, and update setMemPoolChildren to
        // include them, and update their setMemPoolParents to include this tx.
        for (; iter != mapNextTx.end() && iter->first.hash == hash; ++iter) {
            const uint256 &childHash = iter->second.ptx->GetHash();
            txiter childIter = mapTx.find(childHash);
            assert(childIter != mapTx.end());
            // We can skip updating entries we've encountered before or that
            // are in the block (which are already accounted for).
            if (setChildren.insert(childIter).second && !setAlreadyIncluded.count(childHash)) {
                UpdateChild(it, childIter, true);
                UpdateParent(childIter, it, true);
            }
        }
        if (!UpdateForDescendants(it, 100, mapMemPoolDescendantsToUpdate, setAlreadyIncluded)) {
            // Mark as dirty if we can't do the calculation.
            mapTx.modify(it, set_dirty());
        }
    }
}

bool CTxMemPool::CalculateMemPoolAncestors(const CTxMemPoolEntry &entry, setEntries &setAncestors, uint64_t limitAncestorCount, uint64_t limitAncestorSize, uint64_t limitDescendantCount, uint64_t limitDescendantSize, std::string &errString, bool fSearchForParents /* = true */)
{
    setEntries parentHashes;
    const CTransaction &tx = entry.GetTx();

    if (fSearchForParents) {
        // Get parents of this transaction that are in the mempool
        // GetMemPoolParents() is only valid for entries in the mempool, so we
        // iterate mapTx to find parents.
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            txiter piter = mapTx.find(tx.vin[i].prevout.hash);
            if (piter != mapTx.end()) {
                parentHashes.insert(piter);
                if (parentHashes.size() + 1 > limitAncestorCount) {
                    errString = strprintf("too many unconfirmed parents [limit: %u]", limitAncestorCount);
                    return false;
                }
            }
        }
    } else {
        // If we're not searching for parents, we require this to be an
        // entry in the mempool already.
        txiter it = mapTx.iterator_to(entry);
        parentHashes = GetMemPoolParents(it);
    }

    size_t totalSizeWithAncestors = entry.GetTxSize();

    while (!parentHashes.empty()) {
        txiter stageit = *parentHashes.begin();

        setAncestors.insert(stageit);
        parentHashes.erase(stageit);
        totalSizeWithAncestors += stageit->GetTxSize();

        if (stageit->GetSizeWithDescendants() + entry.GetTxSize() > limitDescendantSize) {
            errString = strprintf("exceeds descendant size limit for tx %s [limit: %u]", stageit->GetTx().GetHash().ToString(), limitDescendantSize);
            return false;
        } else if (stageit->GetCountWithDescendants() + 1 > limitDescendantCount) {
            errString = strprintf("too many descendants for tx %s [limit: %u]", stageit->GetTx().GetHash().ToString(), limitDescendantCount);
            return false;
        } else if (totalSizeWithAncestors > limitAncestorSize) {
            errString = strprintf("exceeds ancestor size limit [limit: %u]", limitAncestorSize);
            return false;
        }

        const setEntries & setMemPoolParents = GetMemPoolParents(stageit);
        for (const txiter &phash : setMemPoolParents) {
            // If this is a new ancestor, add it.
            if (setAncestors.count(phash) == 0) {
                parentHashes.insert(phash);
            }
            if (parentHashes.size() + setAncestors.size() + 1 > limitAncestorCount) {
                errString = strprintf("too many unconfirmed ancestors [limit: %u]", limitAncestorCount);
                return false;
            }
        }
    }

    return true;
}

void CTxMemPool::UpdateAncestorsOf(bool add, txiter it, const setEntries &setAncestors)
{
    setEntries parentIters = GetMemPoolParents(it);
    // add or remove this tx as a child of each parent
    for (txiter piter : parentIters) {
        UpdateChild(piter, it, add);
    }
    const int64_t updateCount = (add ? 1 : -1);
    const int64_t updateSize = updateCount * it->GetTxSize();
    const CAmount updateFee = updateCount * it->GetModifiedFee();
    for (txiter ancestorIt : setAncestors) {
        mapTx.modify(ancestorIt, update_descendant_state(updateSize, updateFee, updateCount));
    }
}

void CTxMemPool::UpdateChildrenForRemoval(txiter it)
{
    const setEntries &setMemPoolChildren = GetMemPoolChildren(it);
    for (txiter updateIt : setMemPoolChildren) {
        UpdateParent(updateIt, it, false);
    }
}

void CTxMemPool::UpdateForRemoveFromMempool(const setEntries &entriesToRemove)
{
    // For each entry, walk back all ancestors and decrement size associated with this
    // transaction
    const uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
    for (txiter removeIt : entriesToRemove) {
        setEntries setAncestors;
        const CTxMemPoolEntry &entry = *removeIt;
        std::string dummy;
        // Since this is a tx that is already in the mempool, we can call CMPA
        // with fSearchForParents = false.  If the mempool is in a consistent
        // state, then using true or false should both be correct, though false
        // should be a bit faster.
        // However, if we happen to be in the middle of processing a reorg, then
        // the mempool can be in an inconsistent state.  In this case, the set
        // of ancestors reachable via mapLinks will be the same as the set of 
        // ancestors whose packages include this transaction, because when we
        // add a new transaction to the mempool in addUnchecked(), we assume it
        // has no children, and in the case of a reorg where that assumption is
        // false, the in-mempool children aren't linked to the in-block tx's
        // until UpdateTransactionsFromBlock() is called.
        // So if we're being called during a reorg, ie before
        // UpdateTransactionsFromBlock() has been called, then mapLinks[] will
        // differ from the set of mempool parents we'd calculate by searching,
        // and it's important that we use the mapLinks[] notion of ancestor
        // transactions as the set of things to update for removal.
        CalculateMemPoolAncestors(entry, setAncestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);
        // Note that UpdateAncestorsOf severs the child links that point to
        // removeIt in the entries for the parents of removeIt.  This is
        // fine since we don't need to use the mempool children of any entries
        // to walk back over our ancestors (but we do need the mempool
        // parents!)
        UpdateAncestorsOf(false, removeIt, setAncestors);
    }
    // After updating all the ancestor sizes, we can now sever the link between each
    // transaction being removed and any mempool children (ie, update setMemPoolParents
    // for each direct child of a transaction being removed).
    for (txiter removeIt : entriesToRemove) {
        UpdateChildrenForRemoval(removeIt);
    }
}

void CTxMemPoolEntry::SetDirty()
{
    nCountWithDescendants = 0;
    nSizeWithDescendants = nTxSize;
    nModFeesWithDescendants = GetModifiedFee();
}

void CTxMemPoolEntry::UpdateState(int64_t modifySize, CAmount modifyFee, int64_t modifyCount)
{
    if (!IsDirty()) {
        nSizeWithDescendants += modifySize;
        assert(int64_t(nSizeWithDescendants) > 0);
        nModFeesWithDescendants += modifyFee;
        nCountWithDescendants += modifyCount;
        assert(int64_t(nCountWithDescendants) > 0);
    }
}

CTxMemPool::CTxMemPool() :
    nTransactionsUpdated(0),
    m_utxo(nullptr),
    m_dspStorage(new DoubleSpendProofStorage())
{
    _clear(); //lock free clear
}

CTxMemPool::~CTxMemPool()
{
    delete m_dspStorage;
}

unsigned int CTxMemPool::GetTransactionsUpdated() const
{
    LOCK(cs);
    return nTransactionsUpdated;
}

void CTxMemPool::AddTransactionsUpdated(unsigned int n)
{
    LOCK(cs);
    nTransactionsUpdated += n;
}

void CTxMemPool::addUnchecked(const uint256& hash, const CTxMemPoolEntry &entry, const setEntries &setAncestors)
{
    // Add to memory pool without checking anything.
    // Used by insertTx via TxValidationState which DOES do
    // all the appropriate checks.
    LOCK(cs);
    indexed_transaction_set::iterator newit = mapTx.insert(entry).first;
    mapLinks.insert(make_pair(newit, TxLinks()));

    // Update transaction for any feeDelta created by PrioritiseTransaction
    // TODO: refactor so that the fee delta is calculated before inserting
    // into mapTx.
    std::map<uint256, std::pair<double, CAmount> >::const_iterator pos = mapDeltas.find(hash);
    if (pos != mapDeltas.end()) {
        const std::pair<double, CAmount> &deltas = pos->second;
        if (deltas.second) {
            mapTx.modify(newit, update_fee_delta(deltas.second));
        }
    }

    // Update cachedInnerUsage to include contained transaction's usage.
    // (When we update the entry for in-mempool parents, memory usage will be
    // further updated.)
    cachedInnerUsage += entry.DynamicMemoryUsage();

    const CTransaction& tx = newit->GetTx();
    std::set<uint256> setParentTransactions;
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        mapNextTx[tx.vin[i].prevout] = CInPoint(&tx, entry.tx, i);
        setParentTransactions.insert(tx.vin[i].prevout.hash);
    }
    // Don't bother worrying about child transactions of this one.
    // Normal case of a new transaction arriving is that there can't be any
    // children, because such children would be orphans.
    // An exception to that is if a transaction enters that used to be in a block.
    // In that case, our disconnect block logic will call UpdateTransactionsFromBlock
    // to clean up the mess we're leaving here.

    // Update ancestors with information about this tx
    for  (const uint256 &phash : setParentTransactions) {
        txiter pit = mapTx.find(phash);
        if (pit != mapTx.end()) {
            UpdateParent(newit, pit, true);
        }
    }
    UpdateAncestorsOf(true, newit, setAncestors);

    nTransactionsUpdated++;
    totalTxSize += entry.GetTxSize();
}

bool CTxMemPool::insertTx(CTxMemPoolEntry &entry)
{
    assert(m_utxo);
    assert(entry.dsproof == -1);
    LOCK(cs);

    uint256 hash = entry.tx.createHash();

    if (exists(hash))
        return false;

    std::list<std::pair<int, int> > rescuedOrphans;
    for (const CTxIn &txin : entry.oldTx.vin) { // find double spends.
        auto orphans = m_dspStorage->findOrphans(txin.prevout);
        if (!orphans.empty()) {
            for (auto o : orphans)
                rescuedOrphans.push_back(o);
            // if we find this here, AS AN ORPHAN, then nothing has entered the mempool yet
            // that claimed it. As such we don't have to check for conflicts.
            assert(mapNextTx.find(txin.prevout) == mapNextTx.end()); // Check anyway
            continue;
        }
        auto oldTx = mapNextTx.find(txin.prevout);
        if (oldTx != mapNextTx.end()) { // double spend detected!
            auto iter = mapTx.find(oldTx->second.tx.createHash());
            assert(mapTx.end() != iter);
            int newProofId = -1;
            try {
                if (iter->dsproof == -1) { // no DS proof exists, lets make one.
                    auto item = *iter;
                    logWarning(Log::DSProof) << "Double spend found, creating double spend proof"
                                           << oldTx->second.tx.createHash()
                                           << entry.tx.createHash();
                    item.dsproof = m_dspStorage->add(DoubleSpendProof::create(oldTx->second.tx, entry.tx));
                    mapTx.replace(iter, item);
                    newProofId = item.dsproof;
#ifndef NDEBUG
                    auto newIter = mapTx.find(oldTx->second.tx.createHash());
                    assert(newIter->dsproof == newProofId);
#endif
                }
            } catch (const std::runtime_error &e) {
                // we don't support 100% of the types of transactions yet, failures are possible.
                logInfo(Log::DSProof) << "Failed creating a proof:" << e;
                throw Validation::Exception("Tx double spends another", 0);
            }
            throw Validation::DoubleSpendException(oldTx->second.tx, newProofId);
        }

        auto iter = mapTx.find(txin.prevout.hash);
        if (iter != mapTx.end()) {
            const CTransaction &prevTx = iter->GetTx();
            if (txin.prevout.n < prevTx.vout.size() && !prevTx.vout[txin.prevout.n].IsNull())
                continue; // found it in mempool.
        }

        const UnspentOutput uo = m_utxo->find(txin.prevout.hash, txin.prevout.n);
        if (!uo.isValid())
            return false;
    }

    addUnchecked(hash, entry);
    for (auto i = rescuedOrphans.begin(); i != rescuedOrphans.end(); ++i) {
        const int proofId = i->first;
        auto dsp = m_dspStorage->proof(proofId);
        logDebug(Log::DSProof) << "Rescued a DSP orphan" << dsp.createHash();
        auto rc = dsp.validate(mempool);

        // it can't be missing utxo or transaction, assert we are internally consistent.
        assert(rc == DoubleSpendProof::Valid
               || rc == DoubleSpendProof::Invalid);

        if (rc == DoubleSpendProof::Valid) {
            logDebug(Log::DSProof) << "  Using it, it validated just fine";
            m_dspStorage->claimOrphan(proofId);
            entry.dsproof = proofId;
            txiter iter = mapTx.find(hash);
            mapTx.replace(iter, entry);

            while (++i != rescuedOrphans.end()) {
                logDebug(Log::DSProof) << "Killing orphans, we don't need more than one";
                m_dspStorage->remove(i->first);
            }
            return true;
        } else {
            logDebug(Log::DSProof) << "  DSP didn't validate!" << dsp.createHash();
            m_dspStorage->remove(proofId);

            LOCK(cs_main);
            Misbehaving(i->second, 10);
        }
    }

    return true;
}

void CTxMemPool::removeUnchecked(txiter it)
{
    if (it->dsproof != -1)
        m_dspStorage->remove(it->dsproof);
    for (const CTxIn& txin : it->GetTx().vin)
        mapNextTx.erase(txin.prevout);

    totalTxSize -= it->GetTxSize();
    cachedInnerUsage -= it->DynamicMemoryUsage();
    cachedInnerUsage -= memusage::DynamicUsage(mapLinks[it].parents) + memusage::DynamicUsage(mapLinks[it].children);
    mapLinks.erase(it);
    mapTx.erase(it);
    nTransactionsUpdated++;
}

// Calculates descendants of entry that are not already in setDescendants, and adds to
// setDescendants. Assumes entryit is already a tx in the mempool and setMemPoolChildren
// is correct for tx and all descendants.
// Also assumes that if an entry is in setDescendants already, then all
// in-mempool descendants of it are already in setDescendants as well, so that we
// can save time by not iterating over those entries.
void CTxMemPool::CalculateDescendants(txiter entryit, setEntries &setDescendants)
{
    setEntries stage;
    if (setDescendants.count(entryit) == 0) {
        stage.insert(entryit);
    }
    // Traverse down the children of entry, only adding children that are not
    // accounted for in setDescendants already (because those children have either
    // already been walked, or will be walked in this iteration).
    while (!stage.empty()) {
        txiter it = *stage.begin();
        setDescendants.insert(it);
        stage.erase(it);

        const setEntries &setChildren = GetMemPoolChildren(it);
        for (const txiter &childiter : setChildren) {
            if (!setDescendants.count(childiter)) {
                stage.insert(childiter);
            }
        }
    }
}

void CTxMemPool::remove(const CTransaction &origTx, std::list<CTransaction>& removed, bool fRecursive)
{
    // Remove transaction from memory pool
    {
        LOCK(cs);
        setEntries txToRemove;
        txiter origit = mapTx.find(origTx.GetHash());
        if (origit != mapTx.end()) {
            txToRemove.insert(origit);
        } else if (fRecursive) {
            // If recursively removing but origTx isn't in the mempool
            // be sure to remove any children that are in the pool. This can
            // happen during chain re-orgs if origTx isn't re-accepted into
            // the mempool for any reason.
            for (unsigned int i = 0; i < origTx.vout.size(); i++) {
                std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(origTx.GetHash(), i));
                if (it == mapNextTx.end())
                    continue;
                txiter nextit = mapTx.find(it->second.ptx->GetHash());
                assert(nextit != mapTx.end());
                txToRemove.insert(nextit);
            }
        }
        setEntries setAllRemoves;
        if (fRecursive) {
            for (txiter it : txToRemove) {
                CalculateDescendants(it, setAllRemoves);
            }
        } else {
            setAllRemoves.swap(txToRemove);
        }
        for (txiter it : setAllRemoves) {
            removed.push_back(it->GetTx());
        }
        RemoveStaged(setAllRemoves);
    }
}

void CTxMemPool::removeForReorg(unsigned int nMemPoolHeight, int flags)
{
    // Remove transactions spending a coinbase which are now immature and no-longer-final transactions
    LOCK(cs);
    std::list<CTransaction> transactionsToRemove;
    for (indexed_transaction_set::const_iterator it = mapTx.begin(); it != mapTx.end(); it++) {
        const CTransaction& tx = it->GetTx();
        LockPoints lp = it->GetLockPoints();
        bool validLP =  TestLockPointValidity(&lp);
        if (!CheckFinalTx(tx, flags) || !CheckSequenceLocks(*this, tx, flags, &lp, validLP)) {
            // Note if CheckSequenceLocks fails the LockPoints may still be invalid
            // So it's critical that we remove the tx and not depend on the LockPoints.
            transactionsToRemove.push_back(tx);
        } else if (it->GetSpendsCoinbase()) {
            for (const CTxIn& txin : tx.vin) {
                indexed_transaction_set::const_iterator it2 = mapTx.find(txin.prevout.hash);
                if (it2 != mapTx.end())
                    continue;

                const UnspentOutput uo = m_utxo->find(txin.prevout.hash, txin.prevout.n);
                if (!uo.isValid() || (uo.isCoinbase() && ((signed long)nMemPoolHeight) - uo.blockHeight() < COINBASE_MATURITY)) {
                    transactionsToRemove.push_back(tx);
                    break;
                }
            }
        }
        if (!validLP) {
            mapTx.modify(it, update_lock_points(lp));
        }
    }
    for (const CTransaction& tx : transactionsToRemove) {
        std::list<CTransaction> removed;
        remove(tx, removed, true);
    }
}

void CTxMemPool::removeConflicts(const CTransaction &tx, std::list<CTransaction>& removed)
{
    // Remove transactions which depend on inputs of tx, recursively
    LOCK(cs);
    for (const CTxIn &txin : tx.vin) {
        std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(txin.prevout);
        if (it != mapNextTx.end()) {
            const CTransaction &txConflict = *it->second.ptx;
            if (txConflict != tx) {
                remove(txConflict, removed, true);
                ClearPrioritisation(txConflict.GetHash());
            }
        }
    }
}

/**
 * Called when a block is connected. Removes from mempool and updates the miner fee estimator.
 */
void CTxMemPool::removeForBlock(const std::vector<CTransaction> &vtx, std::list<CTransaction> &conflicts)
{
    LOCK(cs);
    for (const CTransaction& tx : vtx) {
        std::list<CTransaction> dummy;
        remove(tx, dummy, false);
        removeConflicts(tx, conflicts);
        ClearPrioritisation(tx.GetHash());
    }
}

void CTxMemPool::_clear()
{
    mapLinks.clear();
    mapTx.clear();
    mapNextTx.clear();
    totalTxSize = 0;
    cachedInnerUsage = 0;
    ++nTransactionsUpdated;
}

void CTxMemPool::clear()
{
    LOCK(cs);
    _clear();
}

void CTxMemPool::queryHashes(std::vector<uint256>& vtxid)
{
    vtxid.clear();

    LOCK(cs);
    vtxid.reserve(mapTx.size());
    for (indexed_transaction_set::iterator mi = mapTx.begin(); mi != mapTx.end(); ++mi)
        vtxid.push_back(mi->GetTx().GetHash());
}

bool CTxMemPool::lookup(const uint256 &hash, CTransaction &result) const
{
    LOCK(cs);
    indexed_transaction_set::const_iterator i = mapTx.find(hash);
    if (i == mapTx.end()) return false;
    result = i->GetTx();
    return true;
}

bool CTxMemPool::lookup(const uint256 &hash, Tx& result) const
{
    LOCK(cs);
    indexed_transaction_set::const_iterator i = mapTx.find(hash);
    if (i == mapTx.end()) return false;
    result = i->tx;
    return true;
}

bool CTxMemPool::lookup(const COutPoint &outpoint, Tx& result) const
{
    LOCK(cs);

    auto oldTx = mapNextTx.find(outpoint);
    if (oldTx == mapNextTx.end())
        return false;
    result = oldTx->second.tx;
    return true;
}

void CTxMemPool::PrioritiseTransaction(const uint256 hash, const std::string strHash, double dPriorityDelta, const CAmount& nFeeDelta)
{
    {
        LOCK(cs);
        std::pair<double, CAmount> &deltas = mapDeltas[hash];
        deltas.first += dPriorityDelta;
        deltas.second += nFeeDelta;
        txiter it = mapTx.find(hash);
        if (it != mapTx.end()) {
            mapTx.modify(it, update_fee_delta(deltas.second));
            // Now update all ancestors' modified fees with descendants
            setEntries setAncestors;
            uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
            std::string dummy;
            CalculateMemPoolAncestors(*it, setAncestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);
            for (txiter ancestorIt : setAncestors) {
                mapTx.modify(ancestorIt, update_descendant_state(0, nFeeDelta, 0));
            }
        }
    }
    LogPrintf("PrioritiseTransaction: %s priority += %f, fee += %d\n", strHash, dPriorityDelta, FormatMoney(nFeeDelta));
}

void CTxMemPool::ApplyDeltas(const uint256 hash, double &dPriorityDelta, CAmount &nFeeDelta) const
{
    LOCK(cs);
    std::map<uint256, std::pair<double, CAmount> >::const_iterator pos = mapDeltas.find(hash);
    if (pos == mapDeltas.end())
        return;
    const std::pair<double, CAmount> &deltas = pos->second;
    dPriorityDelta += deltas.first;
    nFeeDelta += deltas.second;
}

void CTxMemPool::ClearPrioritisation(const uint256 hash)
{
    LOCK(cs);
    mapDeltas.erase(hash);
}

bool CTxMemPool::HasNoInputsOf(const CTransaction &tx) const
{
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        if (exists(tx.vin[i].prevout.hash))
            return false;
    return true;
}

size_t CTxMemPool::DynamicMemoryUsage() const {
    LOCK(cs);
    // Estimate the overhead of mapTx to be 12 pointers + an allocation, as no exact formula for boost::multi_index_contained is implemented.
    return memusage::MallocUsage(sizeof(CTxMemPoolEntry) + 12 * sizeof(void*)) * mapTx.size() + memusage::DynamicUsage(mapNextTx) + memusage::DynamicUsage(mapDeltas) + memusage::DynamicUsage(mapLinks) + cachedInnerUsage;
}

void CTxMemPool::RemoveStaged(setEntries &stage) {
    AssertLockHeld(cs);
    UpdateForRemoveFromMempool(stage);
    for (const txiter& it : stage) {
        removeUnchecked(it);
    }
}

int CTxMemPool::Expire(int64_t time) {
    LOCK(cs);
    indexed_transaction_set::nth_index<2>::type::iterator it = mapTx.get<2>().begin();
    setEntries toremove;
    while (it != mapTx.get<2>().end() && it->GetTime() < time) {
        toremove.insert(mapTx.project<0>(it));
        it++;
    }
    setEntries stage;
    for (txiter removeit : toremove) {
        CalculateDescendants(removeit, stage);
    }
    RemoveStaged(stage);
    return stage.size();
}

void CTxMemPool::addUnchecked(const uint256&hash, const CTxMemPoolEntry &entry)
{
    LOCK(cs);
    setEntries setAncestors;
    uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
    std::string dummy;
    CalculateMemPoolAncestors(entry, setAncestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy);
    addUnchecked(hash, entry, setAncestors);
}

void CTxMemPool::UpdateChild(txiter entry, txiter child, bool add)
{
    setEntries s;
    if (add && mapLinks[entry].children.insert(child).second) {
        cachedInnerUsage += memusage::IncrementalDynamicUsage(s);
    } else if (!add && mapLinks[entry].children.erase(child)) {
        cachedInnerUsage -= memusage::IncrementalDynamicUsage(s);
    }
}

void CTxMemPool::UpdateParent(txiter entry, txiter parent, bool add)
{
    setEntries s;
    if (add && mapLinks[entry].parents.insert(parent).second) {
        cachedInnerUsage += memusage::IncrementalDynamicUsage(s);
    } else if (!add && mapLinks[entry].parents.erase(parent)) {
        cachedInnerUsage -= memusage::IncrementalDynamicUsage(s);
    }
}

const CTxMemPool::setEntries & CTxMemPool::GetMemPoolParents(txiter entry) const
{
    assert (entry != mapTx.end());
    txlinksMap::const_iterator it = mapLinks.find(entry);
    assert(it != mapLinks.end());
    return it->second.parents;
}

const CTxMemPool::setEntries & CTxMemPool::GetMemPoolChildren(txiter entry) const
{
    assert (entry != mapTx.end());
    txlinksMap::const_iterator it = mapLinks.find(entry);
    assert(it != mapLinks.end());
    return it->second.children;
}

void CTxMemPool::setUtxo(UnspentOutputDatabase *utxo)
{
    assert(utxo);
    m_utxo = utxo;
}

Tx CTxMemPool::addDoubleSpendProof(const DoubleSpendProof &proof)
{
    LOCK(cs);
    auto oldTx = mapNextTx.find(COutPoint(proof.prevTxId(), proof.prevOutIndex()));
    if (oldTx == mapNextTx.end())
        return Tx();

    auto iter = mapTx.find(oldTx->second.tx.createHash());
    assert(mapTx.end() != iter);
    if (iter->dsproof != -1)   // A DSProof already exists for this tx.
        return Tx(); // don't propagate new one.

    auto item = *iter;
    item.dsproof = m_dspStorage->add(proof);
    mapTx.replace(iter, item);

    return oldTx->second.tx;
}

DoubleSpendProofStorage *CTxMemPool::doubleSpendProofStorage() const
{
    return m_dspStorage;
}

CFeeRate CTxMemPool::GetMinFee() const
{
    return CFeeRate();
}

void CTxMemPool::TrimToSize(size_t sizelimit, std::vector<uint256>* pvNoSpendsRemaining) {
    LOCK(cs);

    unsigned nTxnRemoved = 0;
    while (DynamicMemoryUsage() > sizelimit) {
        indexed_transaction_set::nth_index<1>::type::iterator it = mapTx.get<1>().begin();
        setEntries stage;
        CalculateDescendants(mapTx.project<0>(it), stage);
        nTxnRemoved += stage.size();

        std::vector<CTransaction> txn;
        if (pvNoSpendsRemaining) {
            txn.reserve(stage.size());
            for (txiter it : stage)
                txn.push_back(it->GetTx());
        }
        RemoveStaged(stage);
        if (pvNoSpendsRemaining) {
            for (const CTransaction& tx : txn) {
                for (const CTxIn& txin : tx.vin) {
                    if (exists(txin.prevout.hash))
                        continue;
                    std::map<COutPoint, CInPoint>::iterator it = mapNextTx.lower_bound(COutPoint(txin.prevout.hash, 0));
                    if (it == mapNextTx.end() || it->first.hash != txin.prevout.hash)
                        pvNoSpendsRemaining->push_back(txin.prevout.hash);
                }
            }
        }
    }
}
