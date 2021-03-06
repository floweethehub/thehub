/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2015 The Bitcoin Core developers
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

#ifndef FLOWEE_TXMEMPOOL_H
#define FLOWEE_TXMEMPOOL_H

#include <list>
#include <set>

#include "amount.h"
#include "primitives/transaction.h"
#include "sync.h"
#include "primitives/FastTransaction.h"

#include "boost/multi_index_container.hpp"
#include "boost/multi_index/ordered_index.hpp"

class CAutoFile;
class CBlockIndex;
class UnspentOutputDatabase;
class DoubleSpendProofStorage;
class DoubleSpendProof;

inline double AllowFreeThreshold()
{
    return COIN * 144. / 250;
}

inline bool AllowFree(double dPriority)
{
    // Large (in bytes) low-priority (new, small-coin) transactions
    // need a fee.
    return dPriority > AllowFreeThreshold();
}

/** Fake height value used in CCoins to signify they are only in the memory pool (since 0.8) */
static const unsigned int MEMPOOL_HEIGHT = 0x7FFFFFFF;

struct LockPoints
{
    // Will be set to the blockchain height and median time past
    // values that would be necessary to satisfy all relative locktime
    // constraints (BIP68) of this tx given our view of block chain history
    int height;
    int64_t time;
    // As long as the current chain descends from the highest height block
    // containing one of the inputs used in the calculation, then the cached
    // values are still valid even after a reorg.
    CBlockIndex* maxInputBlock;

    LockPoints() : height(0), time(0), maxInputBlock(NULL) { }
};

class CTxMemPool;

/** \class CTxMemPoolEntry
 *
 * CTxMemPoolEntry stores data about the correponding transaction, as well
 * as data about all in-mempool transactions that depend on the transaction
 * ("descendant" transactions).
 *
 * When a new entry is added to the mempool, we update the descendant state
 * (nCountWithDescendants, nSizeWithDescendants, and nModFeesWithDescendants) for
 * all ancestors of the newly added transaction.
 *
 * If updating the descendant state is skipped, we can mark the entry as
 * "dirty", and set nSizeWithDescendants/nModFeesWithDescendants to equal nTxSize/
 * nFee+feeDelta. (This can potentially happen during a reorg, where we limit the
 * amount of work we're willing to do to avoid consuming too much CPU.)
 *
 */

class CTxMemPoolEntry
{
public:
    Tx tx;
    CTransaction oldTx;
    int64_t nFee; //! Cached to avoid expensive parent-transaction lookups
    size_t nTxSize; //! ... and avoid recomputing tx size
    size_t nModSize; //! ... and modified size for priority
    size_t nUsageSize; //! ... and total memory usage
    int64_t nTime; //! Local time when entering the mempool
    double entryPriority; //! Priority when entering the mempool
    unsigned int entryHeight; //! Chain height when entering the mempool
    bool hadNoDependencies; //! Not dependent on any other txs when it entered the mempool
    int64_t inChainInputValue; //! Sum of all txin values that are already in blockchain
    bool spendsCoinbase; //! keep track of transactions that spend a coinbase
    int64_t feeDelta; //! Used for determining the priority of the transaction for mining in a block
    LockPoints lockPoints; //! Track the height and time at which tx was final

    // Information about descendants of this transaction that are in the
    // mempool; if we remove this transaction we must remove all of these
    // descendants as well.  if nCountWithDescendants is 0, treat this entry as
    // dirty, and nSizeWithDescendants and nModFeesWithDescendants will not be
    // correct.
    uint64_t nCountWithDescendants; //! number of descendant transactions
    uint64_t nSizeWithDescendants;  //! ... and size
    int64_t nModFeesWithDescendants;  //! ... and total fees (all including us)
    int dsproof = -1;

    CTxMemPoolEntry(const Tx &tx);

    CTxMemPoolEntry(const CTransaction &tx, int64_t _nFee,
                    int64_t _nTime, double _entryPriority, unsigned int _entryHeight,
                    bool poolHasNoInputsOf, int64_t _inChainInputValue, bool spendsCoinbase,
                    LockPoints lp);
    CTxMemPoolEntry(const CTxMemPoolEntry& other);

    const CTransaction& GetTx() const { return this->oldTx; }
    /**
     * Fast calculation of lower bound of current priority as update
     * from entry priority. Only inputs that were originally in-chain will age.
     */
    double GetPriority(unsigned int currentHeight) const;
    const int64_t& GetFee() const { return nFee; }
    size_t GetTxSize() const { return nTxSize; }
    int64_t GetTime() const { return nTime; }
    unsigned int GetHeight() const { return entryHeight; }
    bool WasClearAtEntry() const { return hadNoDependencies; }
    int64_t GetModifiedFee() const { return nFee + feeDelta; }
    size_t DynamicMemoryUsage() const { return nUsageSize; }
    const LockPoints& GetLockPoints() const { return lockPoints; }

    // Adjusts the descendant state, if this entry is not dirty.
    void UpdateState(int64_t modifySize, int64_t modifyFee, int64_t modifyCount);
    // Updates the fee delta used for mining priority score, and the
    // modified fees with descendants.
    void UpdateFeeDelta(int64_t feeDelta);
    // Update the LockPoints after a reorg
    void UpdateLockPoints(const LockPoints& lp);

    /** We can set the entry to be dirty if doing the full calculation of in-
     *  mempool descendants will be too expensive, which can potentially happen
     *  when re-adding transactions from a block back to the mempool.
     */
    void SetDirty();
    bool IsDirty() const { return nCountWithDescendants == 0; }

    uint64_t GetCountWithDescendants() const { return nCountWithDescendants; }
    uint64_t GetSizeWithDescendants() const { return nSizeWithDescendants; }
    int64_t GetModFeesWithDescendants() const { return nModFeesWithDescendants; }

    bool GetSpendsCoinbase() const { return spendsCoinbase; }
};

// Helpers for modifying CTxMemPool::mapTx, which is a boost multi_index.
struct update_descendant_state
{
    update_descendant_state(int64_t _modifySize, int64_t _modifyFee, int64_t _modifyCount) :
        modifySize(_modifySize), modifyFee(_modifyFee), modifyCount(_modifyCount)
    {}

    void operator() (CTxMemPoolEntry &e)
        { e.UpdateState(modifySize, modifyFee, modifyCount); }

    private:
        int64_t modifySize;
        int64_t modifyFee;
        int64_t modifyCount;
};

struct set_dirty
{
    void operator() (CTxMemPoolEntry &e)
        { e.SetDirty(); }
};

struct update_fee_delta
{
    update_fee_delta(int64_t _feeDelta) : feeDelta(_feeDelta) { }

    void operator() (CTxMemPoolEntry &e) { e.UpdateFeeDelta(feeDelta); }

private:
    int64_t feeDelta;
};

struct update_lock_points
{
    update_lock_points(const LockPoints& _lp) : lp(_lp) { }

    void operator() (CTxMemPoolEntry &e) { e.UpdateLockPoints(lp); }

private:
    const LockPoints& lp;
};

// extracts a TxMemPoolEntry's transaction hash
struct mempoolentry_txid
{
    typedef uint256 result_type;
    result_type operator() (const CTxMemPoolEntry &entry) const
    {
        return entry.GetTx().GetHash();
    }
};

/** \class CompareTxMemPoolEntryByDescendantScore
 *
 *  Sort an entry by max(score/size of entry's tx, score/size with all descendants).
 */
class CompareTxMemPoolEntryByDescendantScore
{
public:
    bool operator()(const CTxMemPoolEntry& a, const CTxMemPoolEntry& b) const
    {
        bool fUseADescendants = UseDescendantScore(a);
        bool fUseBDescendants = UseDescendantScore(b);

        double aModFee = fUseADescendants ? a.GetModFeesWithDescendants() : a.GetModifiedFee();
        double aSize = fUseADescendants ? a.GetSizeWithDescendants() : a.GetTxSize();

        double bModFee = fUseBDescendants ? b.GetModFeesWithDescendants() : b.GetModifiedFee();
        double bSize = fUseBDescendants ? b.GetSizeWithDescendants() : b.GetTxSize();

        // Avoid division by rewriting (a/b > c/d) as (a*d > c*b).
        double f1 = aModFee * bSize;
        double f2 = aSize * bModFee;

        if (f1 == f2) {
            return a.GetTime() >= b.GetTime();
        }
        return f1 < f2;
    }

    // Calculate which score to use for an entry (avoiding division).
    bool UseDescendantScore(const CTxMemPoolEntry &a) const
    {
        double f1 = (double)a.GetModifiedFee() * a.GetSizeWithDescendants();
        double f2 = (double)a.GetModFeesWithDescendants() * a.GetTxSize();
        return f2 > f1;
    }
};

/** \class CompareTxMemPoolEntryByScore
 *
 *  Sort by score of entry ((fee+delta)/size) in descending order
 */
class CompareTxMemPoolEntryByScore
{
public:
    bool operator()(const CTxMemPoolEntry& a, const CTxMemPoolEntry& b) const
    {
        double f1 = (double)a.GetModifiedFee() * b.GetTxSize();
        double f2 = (double)b.GetModifiedFee() * a.GetTxSize();
        if (f1 == f2) {
            return b.GetTx().GetHash() < a.GetTx().GetHash();
        }
        return f1 > f2;
    }
};

class CompareTxMemPoolEntryByEntryTime
{
public:
    bool operator()(const CTxMemPoolEntry& a, const CTxMemPoolEntry& b) const
    {
        return a.GetTime() < b.GetTime();
    }
};

/** An inpoint - a combination of a transaction and an index n into its vin */
class CInPoint
{
public:
    const CTransaction* ptx;
    Tx tx;
    uint32_t n;

    CInPoint() { SetNull(); }
    CInPoint(const CTransaction* ptxIn, const Tx &txIn, uint32_t nIn) { ptx = ptxIn; tx = txIn; n = nIn; }
    void SetNull() { ptx = NULL; n = (uint32_t) -1; }
    bool IsNull() const { return (ptx == NULL && n == (uint32_t) -1); }
};

/**
 * CTxMemPool stores valid-according-to-the-current-best-chain
 * transactions that may be included in the next block.
 *
 * Transactions are added when they are seen on the network
 * (or created by the local node), but not all transactions seen
 * are added to the pool: if a new transaction double-spends
 * an input of a transaction in the pool, it is dropped,
 * as are non-standard transactions.
 *
 * CTxMemPool::mapTx, and CTxMemPoolEntry bookkeeping:
 *
 * mapTx is a boost::multi_index that sorts the mempool on 4 criteria:
 * - transaction hash
 * - feerate [we use max(feerate of tx, feerate of tx with all descendants)]
 * - time in mempool
 * - mining score (feerate modified by any fee deltas from PrioritiseTransaction)
 *
 * Note: the term "descendant" refers to in-mempool transactions that depend on
 * this one, while "ancestor" refers to in-mempool transactions that a given
 * transaction depends on.
 *
 * In order for the feerate sort to remain correct, we must update transactions
 * in the mempool when new descendants arrive.  To facilitate this, we track
 * the set of in-mempool direct parents and direct children in mapLinks.  Within
 * each CTxMemPoolEntry, we track the size and fees of all descendants.
 *
 * Usually when a new transaction is added to the mempool, it has no in-mempool
 * children (because any such children would be an orphan).  So in
 * addUnchecked(), we:
 * - update a new entry's setMemPoolParents to include all in-mempool parents
 * - update the new entry's direct parents to include the new tx as a child
 * - update all ancestors of the transaction to include the new tx's size/fee
 *
 * When a transaction is removed from the mempool, we must:
 * - update all in-mempool parents to not track the tx in setMemPoolChildren
 * - update all ancestors to not include the tx's size/fees in descendant state
 * - update all in-mempool children to not include it as a parent
 *
 * These happen in UpdateForRemoveFromMempool().  (Note that when removing a
 * transaction along with its descendants, we must calculate that set of
 * transactions to be removed before doing the removal, or else the mempool can
 * be in an inconsistent state where it's impossible to walk the ancestors of
 * a transaction.)
 *
 * In the event of a reorg, the assumption that a newly added tx has no
 * in-mempool children is false.  In particular, the mempool is in an
 * inconsistent state while new transactions are being added, because there may
 * be descendant transactions of a tx coming from a disconnected block that are
 * unreachable from just looking at transactions in the mempool (the linking
 * transactions may also be in the disconnected block, waiting to be added).
 * Because of this, there's not much benefit in trying to search for in-mempool
 * children in addUnchecked().  Instead, in the special case of transactions
 * being added from a disconnected block, we require the caller to clean up the
 * state, to account for in-mempool, out-of-block descendants for all the
 * in-block transactions by calling UpdateTransactionsFromBlock().  Note that
 * until this is called, the mempool state is not consistent, and in particular
 * mapLinks may not be correct (and therefore functions like
 * CalculateMemPoolAncestors() and CalculateDescendants() that rely
 * on them to walk the mempool are not generally safe to use).
 *
 * Computational limits:
 *
 * Updating all in-mempool ancestors of a newly added transaction can be slow,
 * if no bound exists on how many in-mempool ancestors there may be.
 * CalculateMemPoolAncestors() takes configurable limits that are designed to
 * prevent these calculations from being too CPU intensive.
 *
 * Adding transactions from a disconnected block can be very time consuming,
 * because we don't have a way to limit the number of in-mempool descendants.
 * To bound CPU processing, we limit the amount of work we're willing to do
 * to properly update the descendant information for a tx being added from
 * a disconnected block.  If we would exceed the limit, then we instead mark
 * the entry as "dirty", and set the feerate for sorting purposes to be equal
 * the feerate of the transaction without any descendants.
 *
 */
class CTxMemPool
{
private:
    unsigned int nTransactionsUpdated;

    uint64_t totalTxSize; //! sum of all mempool tx' byte sizes
    uint64_t cachedInnerUsage; //! sum of dynamic memory usage of all the map elements (NOT the maps themselves)

public:

    static const int ROLLING_FEE_HALFLIFE = 60 * 60 * 12; // public only for testing

    typedef boost::multi_index_container<
        CTxMemPoolEntry,
        boost::multi_index::indexed_by<
            // sorted by txid
            boost::multi_index::ordered_unique<mempoolentry_txid>,
            // sorted by fee rate
            boost::multi_index::ordered_non_unique<
                boost::multi_index::identity<CTxMemPoolEntry>,
                CompareTxMemPoolEntryByDescendantScore
            >,
            // sorted by entry time
            boost::multi_index::ordered_non_unique<
                boost::multi_index::identity<CTxMemPoolEntry>,
                CompareTxMemPoolEntryByEntryTime
                >,
            // sorted by score (for mining prioritization)
            boost::multi_index::ordered_unique<
                boost::multi_index::identity<CTxMemPoolEntry>,
                CompareTxMemPoolEntryByScore
            >
        >
    > indexed_transaction_set;

    mutable CCriticalSection cs;
    indexed_transaction_set mapTx;
    typedef indexed_transaction_set::nth_index<0>::type::iterator txiter;
    struct CompareIteratorByHash {
        bool operator()(const txiter &a, const txiter &b) const {
            return a->GetTx().GetHash() < b->GetTx().GetHash();
        }
    };
    typedef std::set<txiter, CompareIteratorByHash> setEntries;

    const setEntries & GetMemPoolParents(txiter entry) const;
    const setEntries & GetMemPoolChildren(txiter entry) const;

    /// set the backing-UTXO
    void setUtxo(UnspentOutputDatabase *utxo);
    /// returns the backing UTXO
    inline UnspentOutputDatabase *utxo() const {
        return m_utxo;
    }

    /**
     * Add a double spend proof we received elsewhere to an existing mempool-entry.
     * Return Tx of the mempool entry we added this to.
     */
    Tx addDoubleSpendProof(const DoubleSpendProof &proof);

    DoubleSpendProofStorage *doubleSpendProofStorage() const;

    /**
     * @brief doubleSpendProofFor does a lookup on the txid and finds the matching DSP, if it exists.
     * @param txid the transaction-id
     * @param[out] dsp  the double spend proof to fill.
     * @return true if a dsp was found.
     */
    bool doubleSpendProofFor(const uint256 &txid, DoubleSpendProof &dsp);

private:
    typedef std::map<txiter, setEntries, CompareIteratorByHash> cacheMap;

    struct TxLinks {
        setEntries parents;
        setEntries children;
    };

    typedef std::map<txiter, TxLinks, CompareIteratorByHash> txlinksMap;
    txlinksMap mapLinks;

    void UpdateParent(txiter entry, txiter parent, bool add);
    void UpdateChild(txiter entry, txiter child, bool add);

public:
    std::map<COutPoint, CInPoint> mapNextTx;
    std::map<uint256, std::pair<double, int64_t> > mapDeltas; // int64 is the amount in satoshis

    /** Create a new CTxMemPool.
     */
    CTxMemPool();
    ~CTxMemPool();

    // addUnchecked must updated state for all ancestors of a given transaction,
    // to track size/count of descendant transactions.  First version of
    // addUnchecked can be used to have it call CalculateMemPoolAncestors(), and
    // then invoke the second version.
    void addUnchecked(const uint256& hash, const CTxMemPoolEntry &entry);
    void addUnchecked(const uint256& hash, const CTxMemPoolEntry &entry, const setEntries &setAncestors);

    /**
     * Check entry for double spend, and adds if Ok.
     * Notice that the entry.dsproof int gets changed if there was a double-spend-proof already.
     * Throws if something goes wrong.
     */
    bool insertTx(CTxMemPoolEntry &entry);

    inline bool insertTx(Tx &tx) {
        CTxMemPoolEntry entry(tx);
        return insertTx(entry);
    }

    void remove(const CTransaction &tx, std::list<CTransaction>& removed, bool fRecursive = false);
    void removeForReorg(unsigned int nMemPoolHeight, int flags);
    void removeConflicts(const CTransaction &tx, std::list<CTransaction>& removed);
    /**
     * @brief removeForBlock should be called when a block is accepted on-chain which would remove all conflicting transactions from mempool.
     * @param vtx the list of transactions contained in the block.
     * @param conflicts out-variable to be filled with conflicting transactions.
     */
    void removeForBlock(const std::vector<CTransaction>& vtx, std::list<CTransaction>& conflicts);
    void clear();
    void _clear(); //lock free
    void queryHashes(std::vector<uint256>& vtxid);
    unsigned int GetTransactionsUpdated() const;
    void AddTransactionsUpdated(unsigned int n);
    /**
     * Check that none of this transactions inputs are in the mempool, and thus
     * the tx is not dependent on other mempool transactions to be included in a block.
     */
    bool HasNoInputsOf(const CTransaction& tx) const;

    /** Affect CreateNewBlock prioritisation of transactions */
    void PrioritiseTransaction(const uint256 hash, const std::string strHash, double dPriorityDelta, const int64_t& nFeeDelta);
    void ApplyDeltas(const uint256 hash, double &dPriorityDelta, int64_t &nFeeDelta) const;
    void ClearPrioritisation(const uint256 hash);

public:
    /** Remove a set of transactions from the mempool.
     *  If a transaction is in this set, then all in-mempool descendants must
     *  also be in the set.*/
    void RemoveStaged(setEntries &stage);

    /** When adding transactions from a disconnected block back to the mempool,
     *  new mempool entries may have children in the mempool (which is generally
     *  not the case when otherwise adding transactions).
     *  UpdateTransactionsFromBlock() will find child transactions and update the
     *  descendant state for each transaction in hashesToUpdate (excluding any
     *  child transactions present in hashesToUpdate, which are already accounted
     *  for).  Note: hashesToUpdate should be the set of transactions from the
     *  disconnected block that have been accepted back into the mempool.
     */
    void UpdateTransactionsFromBlock(const std::vector<uint256> &hashesToUpdate);

    /** Try to calculate all in-mempool ancestors of entry.
     *  (these are all calculated including the tx itself)
     *  limitAncestorCount = max number of ancestors
     *  limitAncestorSize = max size of ancestors
     *  limitDescendantCount = max number of descendants any ancestor can have
     *  limitDescendantSize = max size of descendants any ancestor can have
     *  errString = populated with error reason if any limits are hit
     *  fSearchForParents = whether to search a tx's vin for in-mempool parents, or
     *    look up parents from mapLinks. Must be true for entries not in the mempool
     */
    bool CalculateMemPoolAncestors(const CTxMemPoolEntry &entry, setEntries &setAncestors, uint64_t limitAncestorCount, uint64_t limitAncestorSize, uint64_t limitDescendantCount, uint64_t limitDescendantSize, std::string &errString, bool fSearchForParents = true);

    /** The minimum fee to get into the mempool
      */
    CFeeRate GetMinFee() const;

    /** Remove transactions from the mempool until its dynamic size is <= sizelimit.
      *  pvNoSpendsRemaining, if set, will be populated with the list of transactions
      *  which are not in mempool which no longer have any spends in this mempool.
      */
    void TrimToSize(size_t sizelimit, std::vector<uint256>* pvNoSpendsRemaining=NULL);

    /** Expire all transaction (and their dependencies) in the mempool older than time. Return the number of removed transactions. */
    int Expire(int64_t time);

    unsigned long size()
    {
        LOCK(cs);
        return mapTx.size();
    }

    uint64_t GetTotalTxSize()
    {
        LOCK(cs);
        return totalTxSize;
    }

    bool exists(uint256 hash) const
    {
        LOCK(cs);
        return (mapTx.count(hash) != 0);
    }

    /// Fetch the transaction, and optionally its associated dsproof
    bool lookup(const uint256 &hash, Tx &result) const;
    bool lookup(const uint256 &hash, CTransaction &result) const;
    bool lookup(const COutPoint &outpoint, Tx &result) const;

    size_t DynamicMemoryUsage() const;

private:
    /** UpdateForDescendants is used by UpdateTransactionsFromBlock to update
     *  the descendants for a single transaction that has been added to the
     *  mempool but may have child transactions in the mempool, eg during a
     *  chain reorg.  setExclude is the set of descendant transactions in the
     *  mempool that must not be accounted for (because any descendants in
     *  setExclude were added to the mempool after the transaction being
     *  updated and hence their state is already reflected in the parent
     *  state).
     *
     *  If updating an entry requires looking at more than maxDescendantsToVisit
     *  transactions, outside of the ones in setExclude, then give up.
     *
     *  cachedDescendants will be updated with the descendants of the transaction
     *  being updated, so that future invocations don't need to walk the
     *  same transaction again, if encountered in another transaction chain.
     */
    bool UpdateForDescendants(txiter updateIt,
            int maxDescendantsToVisit,
            cacheMap &cachedDescendants,
            const std::set<uint256> &setExclude);
    /** Update ancestors of hash to add/remove it as a descendant transaction. */
    void UpdateAncestorsOf(bool add, txiter hash, const setEntries &setAncestors);
    /** For each transaction being removed, update ancestors and any direct children. */
    void UpdateForRemoveFromMempool(const setEntries &entriesToRemove);
    /** Sever link between specified transaction and direct children. */
    void UpdateChildrenForRemoval(txiter entry);

    /** Populate setDescendants with all in-mempool descendants of hash.
     *  Assumes that setDescendants includes all in-mempool descendants of anything
     *  already in it.  */
    void CalculateDescendants(txiter it, setEntries &setDescendants);

    /** Before calling removeUnchecked for a given transaction,
     *  UpdateForRemoveFromMempool must be called on the entire (dependent) set
     *  of transactions being removed at the same time.  We use each
     *  CTxMemPoolEntry's setMemPoolParents in order to walk ancestors of a
     *  given transaction that is removed, so we can't remove intermediate
     *  transactions in a chain before we've updated all the state for the
     *  removal.
     */
    void removeUnchecked(txiter entry);

    UnspentOutputDatabase *m_utxo;
    DoubleSpendProofStorage *m_dspStorage;
};

// We want to sort transactions by coin age priority
typedef std::pair<double, CTxMemPool::txiter> TxCoinAgePriority;

struct TxCoinAgePriorityCompare
{
    bool operator()(const TxCoinAgePriority& a, const TxCoinAgePriority& b)
    {
        if (a.first == b.first)
            return CompareTxMemPoolEntryByScore()(*(b.second), *(a.second)); //Reverse order to make sort less than
        return a.first < b.first;
    }
};

#endif
