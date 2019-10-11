/*
 * This file is part of the Flowee project
 * Copyright (c) 2009-2010 Satoshi Nakamoto
 * Copyright (c) 2009-2015 The Bitcoin Core developers
 * Copyright (C) 2017-2019 Tom Zander <tomz@freedommail.ch>
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

#include "main.h"

#include "Application.h"
#include "SettingsDefaults.h"
#include "addrman.h"
#include "Application.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "DoubleSpendProof.h"
#include "DoubleSpendProofStorage.h"
#include "hash.h"
#include "init.h"
#include "serverutil.h"
#include "merkleblock.h"
#include "policy/policy.h"
#include "script/sigcache.h"
#include "thinblock.h"
#include "txmempool.h"
#include "txorphancache.h"
#include "UiInterface.h"
#include "undo.h"
#include "utilmoneystr.h"
#include "validationinterface.h"
#include "primitives/FastBlock.h"
#include <validation/Engine.h>
#include <utxo/UnspentOutputDatabase.h>
#include <BlocksDB.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/math/distributions/poisson.hpp>

/**
 * Global state
 */

CCriticalSection cs_main;

CChain chainActive;
CBlockIndex *pindexBestHeader = nullptr;
CWaitableCriticalSection csBestBlock;
CConditionVariable cvBlockChange;
bool fIsBareMultisigStd = Settings::DefaultPermitBareMultisig;
bool fRequireStandard = true;
unsigned int nBytesPerSigOp = Settings::DefaultBytesPerSigop;
bool fCheckpointsEnabled = Settings::DefaultCheckpointsEnabled;

/** Fees smaller than this (in satoshi) are considered zero fee (for relaying, mining and transaction creation) */
CFeeRate minRelayTxFee = CFeeRate(Settings::DefaultMinRelayTxFee);

CTxMemPool mempool;

const std::string strMessageMagic = "Bitcoin Signed Message:\n";

CCriticalSection cs_LastBlockFile;
std::vector<CBlockFileInfo> vinfoBlockFile;
int nLastBlockFile = 0;
/** Dirty block file entries. */
std::set<int> setDirtyFileInfo;

// Internal stuff
namespace {

    struct CBlockIndexWorkComparator
    {
        bool operator()(CBlockIndex *pa, CBlockIndex *pb) const {
            // First sort by most total work, ...
            if (pa->nChainWork > pb->nChainWork) return false;
            if (pa->nChainWork < pb->nChainWork) return true;

            // ... then by earliest time received, ...
            if (pa->nSequenceId < pb->nSequenceId) return false;
            if (pa->nSequenceId > pb->nSequenceId) return true;

            // Use pointer address as tie breaker (should only happen with blocks
            // loaded from disk, as those all have id 0).
            if (pa < pb) return false;
            if (pa > pb) return true;

            // Identical blocks.
            return false;
        }
    };

    CBlockIndex *pindexBestInvalid;

    /**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
     * missing the data for the block.
     */
    std::set<CBlockIndex*, CBlockIndexWorkComparator> setBlockIndexCandidates;
    /** Number of nodes with fSyncStarted. */
    int nSyncStarted = 0;

    /**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork.
     */
    CCriticalSection cs_nBlockSequenceId;
    /** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
    uint32_t nBlockSequenceId = 1;

    /**
     * Sources of received blocks, saved to be able to send them reject
     * messages or ban them when processing happens afterwards. Protected by
     * cs_main.
     */
    std::map<uint256, NodeId> mapBlockSource;

    /**
     * Filter for transactions that were recently rejected by
     * AcceptToMemoryPool. These are not rerequested until the chain tip
     * changes, at which point the entire filter is reset. Protected by
     * cs_main.
     *
     * Without this filter we'd be re-requesting txs from each of our peers,
     * increasing bandwidth consumption considerably. For instance, with 100
     * peers, half of which relay a tx we don't accept, that might be a 50x
     * bandwidth increase. A flooding attacker attempting to roll-over the
     * filter using minimum-sized, 60byte, transactions might manage to send
     * 1000/sec if we have fast peers, so we pick 120,000 to give our peers a
     * two minute window to send invs to us.
     *
     * Decreasing the false positive rate is fairly cheap, so we pick one in a
     * million to make it highly unlikely for users to have issues with this
     * filter.
     *
     * Memory used: 1.7MB
     */
    boost::scoped_ptr<CRollingBloomFilter> recentRejects;
    uint256 hashRecentRejectsChainTip;

    /** Blocks that are in flight, and that are in the queue to be downloaded. Protected by cs_main. */
    struct QueuedBlock {
        uint256 hash;
        CBlockIndex* pindex;     //!< Optional.
        bool fValidatedHeaders;  //!< Whether this block has validated headers at the time of request.
    };
    std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> > mapBlocksInFlight;

    /** Number of preferable block download peers. */
    int nPreferredDownload = 0;

    /** Dirty block index entries. The block index instances not yet persisted to (index) DB */
    std::set<CBlockIndex*> setDirtyBlockIndex;

    /** Number of peers from which we're downloading blocks. */
    int nPeersWithValidatedDownloads = 0;
} // anon namespace

//////////////////////////////////////////////////////////////////////////////
//
// Registration of network node signals.
//

namespace {

struct CBlockReject {
    unsigned char chRejectCode;
    std::string strRejectReason;
    uint256 hashBlock;
};

/**
 * Maintain validation-specific state about nodes, protected by cs_main, instead
 * by CNode's own locks. This simplifies asynchronous operation, where
 * processing of incoming data is done after the ProcessMessage call returns,
 * and we're no longer holding the node's locks.
 */
struct CNodeState {
    //! The peer's address
    CService address;
    //! Whether we have a fully established connection.
    bool fCurrentlyConnected;
    //! Accumulated misbehaviour score for this peer.
    int nMisbehavior;
    //! Whether this peer should be disconnected and banned (unless whitelisted).
    bool fShouldBan;
    //! List of asynchronously-determined block rejections to notify this peer about.
    std::vector<CBlockReject> rejects;
    //! The best known block we know this peer has announced.
    CBlockIndex *pindexBestKnownBlock;
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock;
    //! The last full block we both have.
    CBlockIndex *pindexLastCommonBlock;
    //! The best header we have sent our peer.
    CBlockIndex *pindexBestHeaderSent;
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted;
    //! Since when we're stalling block download progress (in microseconds), or 0.
    int64_t nStallingSince;
    std::list<QueuedBlock> vBlocksInFlight;
    //! When the first entry in vBlocksInFlight started downloading. Don't care when vBlocksInFlight is empty.
    int64_t nDownloadingSince;
    int nBlocksInFlight;
    int nBlocksInFlightValidHeaders;
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload;
    //! Whether this peer wants invs or headers (when possible) for block announcements.
    bool fPreferHeaders;

    CNodeState() {
        fCurrentlyConnected = false;
        nMisbehavior = 0;
        fShouldBan = false;
        pindexBestKnownBlock = nullptr;
        hashLastUnknownBlock.SetNull();
        pindexLastCommonBlock = nullptr;
        pindexBestHeaderSent = nullptr;
        fSyncStarted = false;
        nStallingSince = 0;
        nDownloadingSince = 0;
        nBlocksInFlight = 0;
        nBlocksInFlightValidHeaders = 0;
        fPreferredDownload = false;
        fPreferHeaders = false;
    }
};

/** Map maintaining per-node state. Requires cs_main. */
std::map<NodeId, CNodeState> mapNodeState;

// Requires cs_main.
CNodeState *State(NodeId pnode) {
    std::map<NodeId, CNodeState>::iterator it = mapNodeState.find(pnode);
    if (it == mapNodeState.end())
        return nullptr;
    return &it->second;
}

int GetHeight()
{
    LOCK(cs_main);
    return chainActive.Height();
}

void UpdatePreferredDownload(CNode* node, CNodeState* state)
{
    nPreferredDownload -= state->fPreferredDownload;

    // Whether this node should be marked as a preferred download node.
    state->fPreferredDownload = (!node->fInbound || node->fWhitelisted) && !node->fOneShot && !node->fClient;

    nPreferredDownload += state->fPreferredDownload;
}

void InitializeNode(NodeId nodeid, const CNode *pnode) {
    LOCK(cs_main);
    CNodeState &state = mapNodeState.insert(std::make_pair(nodeid, CNodeState())).first->second;
    state.address = pnode->addr;
}

void FinalizeNode(NodeId nodeid) {
    LOCK(cs_main);
    CNodeState *state = State(nodeid);
    assert(state);

    if (state->fSyncStarted)
        nSyncStarted--;

    if (state->nMisbehavior == 0 && state->fCurrentlyConnected) {
        AddressCurrentlyConnected(state->address);
    }

    for (const QueuedBlock& entry : state->vBlocksInFlight) {
        mapBlocksInFlight.erase(entry.hash);
    }
    nPreferredDownload -= state->fPreferredDownload;
    nPeersWithValidatedDownloads -= (state->nBlocksInFlightValidHeaders != 0);
    assert(nPeersWithValidatedDownloads >= 0);

    mapNodeState.erase(nodeid);

    if (mapNodeState.empty()) {
        // Do a consistency check after the last peer is removed.
        assert(mapBlocksInFlight.empty());
        assert(nPreferredDownload == 0);
        assert(nPeersWithValidatedDownloads == 0);
    }
}
}

// Requires cs_main.
// Returns a bool indicating whether we requested this block.
bool MarkBlockAsReceived(const uint256& hash) {
    std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> >::iterator itInFlight = mapBlocksInFlight.find(hash);
    if (itInFlight != mapBlocksInFlight.end()) {
        CNodeState *state = State(itInFlight->second.first);
        state->nBlocksInFlightValidHeaders -= itInFlight->second.second->fValidatedHeaders;
        if (state->nBlocksInFlightValidHeaders == 0 && itInFlight->second.second->fValidatedHeaders) {
            // Last validated block on the queue was received.
            nPeersWithValidatedDownloads--;
        }
        if (state->vBlocksInFlight.begin() == itInFlight->second.second) {
            // First block on the queue was received, update the start download time for the next one
            state->nDownloadingSince = std::max(state->nDownloadingSince, GetTimeMicros());
        }
        state->vBlocksInFlight.erase(itInFlight->second.second);
        state->nBlocksInFlight--;
        state->nStallingSince = 0;
        mapBlocksInFlight.erase(itInFlight);
        return true;
    }
    return false;
}

bool IsBlockInFlight(const uint256 &hash)
{
    return mapBlocksInFlight.count(hash) > 0;
}

namespace {

// Requires cs_main.
void MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, const Consensus::Params&, CBlockIndex *pindex = nullptr) {
    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    // Make sure it's not listed somewhere already.
    MarkBlockAsReceived(hash);

    QueuedBlock newentry = {hash, pindex, pindex != nullptr};
    std::list<QueuedBlock>::iterator it = state->vBlocksInFlight.insert(state->vBlocksInFlight.end(), newentry);
    state->nBlocksInFlight++;
    state->nBlocksInFlightValidHeaders += newentry.fValidatedHeaders;
    if (state->nBlocksInFlight == 1) {
        // We're starting a block download (batch) from this peer.
        state->nDownloadingSince = GetTimeMicros();
    }
    if (state->nBlocksInFlightValidHeaders == 1 && pindex != nullptr) {
        nPeersWithValidatedDownloads++;
    }
    mapBlocksInFlight[hash] = std::make_pair(nodeid, it);
}

/** Check whether the last unknown block a peer advertised is not yet known. */
void ProcessBlockAvailability(NodeId nodeid) {
    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    if (!state->hashLastUnknownBlock.IsNull()) {
        auto bi = Blocks::Index::get(state->hashLastUnknownBlock);
        if (bi && bi->nChainWork > 0) {
            if (state->pindexBestKnownBlock == nullptr || bi->nChainWork >= state->pindexBestKnownBlock->nChainWork)
                state->pindexBestKnownBlock = bi;
            state->hashLastUnknownBlock.SetNull();
        }
    }
}

/** Update tracking information about which blocks a peer is assumed to have. */
void UpdateBlockAvailability(NodeId nodeid, const uint256 &hash) {
    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    ProcessBlockAvailability(nodeid);

    auto bi = Blocks::Index::get(hash);
    if (bi && bi->nChainWork > 0) {
        // An actually better block was announced.
        if (state->pindexBestKnownBlock == nullptr || bi->nChainWork >= state->pindexBestKnownBlock->nChainWork)
            state->pindexBestKnownBlock = bi;
    } else {
        // An unknown block was announced; just assume that the latest one is the best one.
        state->hashLastUnknownBlock = hash;
    }
}

// Requires cs_main
bool CanDirectFetch(const Consensus::Params &consensusParams)
{
    return chainActive.Tip()->GetBlockTime() > GetAdjustedTime() - consensusParams.nPowTargetSpacing * 20;
}

// Requires cs_main
bool PeerHasHeader(CNodeState *state, CBlockIndex *pindex)
{
    if (state->pindexBestKnownBlock && pindex == state->pindexBestKnownBlock->GetAncestor(pindex->nHeight))
        return true;
    if (state->pindexBestHeaderSent && pindex == state->pindexBestHeaderSent->GetAncestor(pindex->nHeight))
        return true;
    return false;
}

/** Update pindexLastCommonBlock and add not-in-flight missing successors to vBlocks, until it has
 *  at most count entries. */
void FindNextBlocksToDownload(NodeId nodeid, unsigned int count, std::vector<CBlockIndex*>& vBlocks, NodeId& nodeStaller) {
    if (count == 0)
        return;

    vBlocks.reserve(vBlocks.size() + count);
    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    // Make sure pindexBestKnownBlock is up to date, we'll need it.
    ProcessBlockAvailability(nodeid);

    if (state->pindexBestKnownBlock == nullptr || state->pindexBestKnownBlock->nChainWork < chainActive.Tip()->nChainWork) {
        // This peer has nothing interesting.
        return;
    }

    if (state->pindexLastCommonBlock == nullptr) {
        // Bootstrap quickly by guessing a parent of our best tip is the forking point.
        // Guessing wrong in either direction is not a problem.
        state->pindexLastCommonBlock = chainActive[std::min(state->pindexBestKnownBlock->nHeight, chainActive.Height())];
    }

    // If the peer reorganized, our previous pindexLastCommonBlock may not be an ancestor
    // of its current tip anymore. Go back enough to fix that.
    state->pindexLastCommonBlock = Blocks::Index::lastCommonAncestor(state->pindexLastCommonBlock, state->pindexBestKnownBlock);
    if (state->pindexLastCommonBlock == state->pindexBestKnownBlock)
        return;

    std::vector<CBlockIndex*> vToFetch;
    CBlockIndex *pindexWalk = state->pindexLastCommonBlock;
    // Never fetch further than the best block we know the peer has, or more than BLOCK_DOWNLOAD_WINDOW + 1 beyond the last
    // linked block we have in common with this peer. The +1 is so we can detect stalling, namely if we would be able to
    // download that next block if the window were 1 larger.
    int nWindowEnd = state->pindexLastCommonBlock->nHeight + BLOCK_DOWNLOAD_WINDOW;
    int nMaxHeight = std::min<int>(state->pindexBestKnownBlock->nHeight, nWindowEnd + 1);
    NodeId waitingfor = -1;
    while (pindexWalk->nHeight < nMaxHeight) {
        // Read up to 128 (or more, if more blocks than that are needed) successors of pindexWalk (towards
        // pindexBestKnownBlock) into vToFetch. We fetch 128, because CBlockIndex::GetAncestor may be as expensive
        // as iterating over ~100 CBlockIndex* entries anyway.
        int nToFetch = std::min(nMaxHeight - pindexWalk->nHeight, std::max<int>(count - vBlocks.size(), 128));
        vToFetch.resize(nToFetch);
        pindexWalk = state->pindexBestKnownBlock->GetAncestor(pindexWalk->nHeight + nToFetch);
        vToFetch[nToFetch - 1] = pindexWalk;
        for (unsigned int i = nToFetch - 1; i > 0; i--) {
            vToFetch[i - 1] = vToFetch[i]->pprev;
        }

        // Iterate over those blocks in vToFetch (in forward direction), adding the ones that
        // are not yet downloaded and not in flight to vBlocks. In the mean time, update
        // pindexLastCommonBlock as long as all ancestors are already downloaded, or if it's
        // already part of our chain (and therefore don't need it even if pruned).
        for (CBlockIndex* pindex : vToFetch) {
            if (!pindex->IsValid(BLOCK_VALID_TREE)) {
                // We consider the chain that this peer is on invalid.
                return;
            }
            if (pindex->nStatus & BLOCK_HAVE_DATA || chainActive.Contains(pindex)) {
                if (pindex->nChainTx)
                    state->pindexLastCommonBlock = pindex;
            } else if (mapBlocksInFlight.count(pindex->GetBlockHash()) == 0) {
                // The block is not already downloaded, and not yet in flight.
                if (pindex->nHeight > nWindowEnd) {
                    // We reached the end of the window.
                    if (vBlocks.size() == 0 && waitingfor != nodeid) {
                        // We aren't able to fetch anything, but we would be if the download window was one larger.
                        nodeStaller = waitingfor;
                    }
                    return;
                }
                vBlocks.push_back(pindex);
                if (vBlocks.size() == count) {
                    return;
                }
            } else if (waitingfor == -1) {
                // This is the first already-in-flight block.
                waitingfor = mapBlocksInFlight[pindex->GetBlockHash()].first;
            }
        }
    }
}

} // anon namespace

bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats) {
    LOCK(cs_main);
    CNodeState *state = State(nodeid);
    if (state == nullptr)
        return false;
    stats.nMisbehavior = state->nMisbehavior;
    stats.nSyncHeight = state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;
    stats.nCommonHeight = state->pindexLastCommonBlock ? state->pindexLastCommonBlock->nHeight : -1;
    for (const QueuedBlock& queue : state->vBlocksInFlight) {
        if (queue.pindex)
            stats.vHeightInFlight.push_back(queue.pindex->nHeight);
    }
    return true;
}

void RegisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.GetHeight.connect(&GetHeight);
    nodeSignals.ProcessMessages.connect(&ProcessMessages);
    nodeSignals.SendMessages.connect(&SendMessages);
    nodeSignals.InitializeNode.connect(&InitializeNode);
    nodeSignals.FinalizeNode.connect(&FinalizeNode);
}

void UnregisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.GetHeight.disconnect(&GetHeight);
    nodeSignals.ProcessMessages.disconnect(&ProcessMessages);
    nodeSignals.SendMessages.disconnect(&SendMessages);
    nodeSignals.InitializeNode.disconnect(&InitializeNode);
    nodeSignals.FinalizeNode.disconnect(&FinalizeNode);
}

CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator)
{
    // Find the first block the caller has in the main chain
    for (const uint256& hash : locator.vHave) {
        auto pindex = Blocks::Index::get(hash);
        if (pindex) {
            if (chain.Contains(pindex))
                return pindex;
        }
    }
    return chain.Genesis();
}

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const CTxIn& txin : tx.vin) {
        if (!(txin.nSequence == CTxIn::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

bool CheckFinalTx(const CTransaction &tx, int flags)
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags.
    flags = std::max(flags, 0);

    // CheckFinalTx() uses chainActive.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than chainActive.Height().
    const int nBlockHeight = chainActive.Height() + 1;

    // BIP113 will require that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST)
                             ? chainActive.Tip()->GetMedianTimePast()
                             : GetAdjustedTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}

/**
 * Calculates the block height and previous block's median time past at
 * which the transaction will be considered final in the context of BIP 68.
 * Also removes from the vector of input heights any entries which did not
 * correspond to sequence locked inputs as they do not affect the calculation.
 */
static std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = static_cast<uint32_t>(tx.nVersion) >= 2
                      && flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn& txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight-1, 0))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

static bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair)
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

bool TestLockPointValidity(const LockPoints* lp)
{
    AssertLockHeld(cs_main);
    assert(lp);
    // If there are relative lock times then the maxInputBlock will be set
    // If there are no relative lock times, the LockPoints don't depend on the chain
    if (lp->maxInputBlock) {
        // Check whether chainActive is an extension of the block at which the LockPoints
        // calculation was valid.  If not LockPoints are no longer valid
        if (!chainActive.Contains(lp->maxInputBlock)) {
            return false;
        }
    }

    // LockPoints still valid
    return true;
}

bool CheckSequenceLocks(CTxMemPool &mp, const CTransaction &tx, int flags, LockPoints* lp, bool useExistingLockPoints, CBlockIndex *tip)
{
    if (!tip) {
        AssertLockHeld(cs_main);
        tip = chainActive.Tip();
    }
    CBlockIndex index;
    index.pprev = tip;
    // CheckSequenceLocks() uses chainActive.Height()+1 to evaluate
    // height based locks because when SequenceLocks() is called within
    // ConnectBlock(), the height of the block *being*
    // evaluated is what is used.
    // Thus if we want to know if a transaction can be part of the
    // *next* block, we need to use one more than chainActive.Height()
    index.nHeight = tip->nHeight + 1;

    std::pair<int, int64_t> lockPair;
    if (useExistingLockPoints) {
        assert(lp);
        lockPair.first = lp->height;
        lockPair.second = lp->time;
    }
    else {
        std::vector<int> prevheights;
        prevheights.resize(tx.vin.size());
        for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
            const CTxIn& txin = tx.vin[txinIndex];

            Tx prevTx;
            if (mp.lookup(txin.prevout.hash, prevTx)) {
                // Assume all mempool transaction confirm in the next block
                prevheights[txinIndex] = tip->nHeight + 1;
            } else {
                // try UTXO
                UnspentOutput output = g_utxo->find(txin.prevout.hash, txin.prevout.n);
                if (!output.isValid())
                    return error("%s: Missing input", __func__);
                prevheights[txinIndex] = output.blockHeight();
            }
        }
        lockPair = CalculateSequenceLocks(tx, flags, &prevheights, index);
        if (lp) {
            lp->height = lockPair.first;
            lp->time = lockPair.second;
            // Also store the hash of the block with the highest height of
            // all the blocks which have sequence locked prevouts.
            // This hash needs to still be on the chain
            // for these LockPoint calculations to be valid
            // Note: It is impossible to correctly calculate a maxInputBlock
            // if any of the sequence locked inputs depend on unconfirmed txs,
            // except in the special case where the relative lock time/height
            // is 0, which is equivalent to no sequence lock. Since we assume
            // input height of tip+1 for mempool txs and test the resulting
            // lockPair from CalculateSequenceLocks against tip+1.  We know
            // EvaluateSequenceLocks will fail if there was a non-zero sequence
            // lock on a mempool input, so we can use the return value of
            // CheckSequenceLocks to indicate the LockPoints validity
            int maxInputHeight = 0;
            for (int height : prevheights) {
                // Can ignore mempool inputs since we'll fail if they had non-zero locks
                if (height != tip->nHeight+1) {
                    maxInputHeight = std::max(maxInputHeight, height);
                }
            }
            lp->maxInputBlock = tip->GetAncestor(maxInputHeight);
        }
    }
    return EvaluateSequenceLocks(index, lockPair);
}

bool CheckTransaction(const CTransaction& tx, CValidationState &state)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");
    if (tx.vout.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty");
    // Size limits
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_TX_SIZE)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    for (const CTxOut& txout : tx.vout) {
        if (txout.nValue < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs
    std::set<COutPoint> vInOutPoints;
    for (const CTxIn& txin : tx.vin) {
        if (vInOutPoints.count(txin.prevout))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
        vInOutPoints.insert(txin.prevout);
    }

    if (tx.IsCoinBase())
    {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
    }
    else
    {
        for (const CTxIn& txin : tx.vin)
            if (txin.prevout.IsNull())
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null");
    }

    return true;
}

void LimitMempoolSize(CTxMemPool& pool, size_t limit, unsigned long age) {
    int expired = pool.Expire(GetTime() - age);
    if (expired != 0)
        LogPrint("mempool", "Expired %i transactions from the memory pool\n", expired);

    pool.TrimToSize(limit, nullptr);
}



//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos, const Consensus::Params& consensusParams)
{
    block.SetNull();

    // Open history file to read
    FastBlock fb = Blocks::DB::instance()->loadBlock(pos);
    if (fb.size() == 0) {
        LogPrintf("ReadBlockFromDisk: Unable to open file %d\n", pos.nFile);
        return false;
    }

    // Read block
    try {
        block = fb.createOldBlock();
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s at %s", __func__, e.what(), pos.ToString());
    }

    // Check the header
    if (!CheckProofOfWork(block.GetHash(), block.nBits, consensusParams))
        return error("ReadBlockFromDisk: Errors in block header at %s", pos.ToString());

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    if (!ReadBlockFromDisk(block, pindex->GetBlockPos(), consensusParams))
        return false;
    if (block.GetHash() != pindex->GetBlockHash())
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s",
                pindex->ToString(), pindex->GetBlockPos().ToString());
    return true;
}

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams)
{
    int halvings = nHeight / consensusParams.nSubsidyHalvingInterval;
    // Force block reward to zero when right shift is undefined.
    if (halvings >= 64)
        return 0;

    CAmount nSubsidy = 50 * COIN;
    // Subsidy is cut in half every 210,000 blocks which will occur approximately every 4 years.
    nSubsidy >>= halvings;
    return nSubsidy;
}

bool IsInitialBlockDownload()
{
    if (Blocks::DB::instance()->isReindexing())
        return true;
    return Blocks::DB::instance()->headerChain().Height() - chainActive.Height() > 1000;
}

void AlertNotify(const std::string& strMessage, bool fThread)
{
    uiInterface.NotifyAlertChanged();
    std::string strCmd = GetArg("-alertnotify", "");
    if (strCmd.empty()) return;

    // Alert text should be plain ascii coming from a trusted source, but to
    // be safe we first strip anything not in safeChars, then add single quotes around
    // the whole string before passing it to the shell:
    std::string singleQuote("'");
    std::string safeStatus = SanitizeString(strMessage);
    safeStatus = singleQuote+safeStatus+singleQuote;
    boost::replace_all(strCmd, "%s", safeStatus);

    if (fThread)
        boost::thread t(runCommand, strCmd); // thread runs free
    else
        runCommand(strCmd);
}

// Requires cs_main.
void Misbehaving(NodeId nodeId, int howmuch)
{
    if (howmuch == 0)
        return;

    CNodeState *state = State(nodeId);
    if (state == nullptr)
        return;

    state->nMisbehavior += howmuch;
    int banscore = GetArg("-banscore", Settings::DefaultBanscoreThreshold);
    if (!state->fShouldBan && state->nMisbehavior >= banscore && state->nMisbehavior - howmuch < banscore) {
        logCritical(Log::Net) << "Id:" << nodeId << state->nMisbehavior-howmuch << "=>" <<  state->nMisbehavior
                    << "Ban threshold exceeded";
        state->fShouldBan = true;
        addrman.increaseUselessness(state->address, 2);
    } else {
        logWarning(Log::Net) << "Misbehaving" << "Id:" << nodeId << state->nMisbehavior-howmuch << "=>" << state->nMisbehavior;
    }
}

void queueRejectMessage(int peerId, const uint256 &blockHash, uint8_t rejectCode, const std::string &rejectReason)
{
    LOCK(cs_main);
    auto state = State(peerId);
    if (state) {
        CBlockReject reject = {rejectCode, rejectReason.substr(0, MAX_REJECT_MESSAGE_LENGTH), blockHash};
        state->rejects.push_back(reject);
    }
}

bool CScriptCheck::operator()() {
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    if (!VerifyScript(scriptSig, scriptPubKey, nFlags, CachingTransactionSignatureChecker(ptxTo, nIn, amount, cacheStore), &error)) {
        return false;
    }
    return true;
}

/** Abort with a message */
bool AbortNode(const std::string& strMessage, const std::string& userMessage="")
{
    strMiscWarning = strMessage;
    LogPrintf("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
        userMessage.empty() ? _("Error: A fatal internal error occurred, see hub.log for details") : userMessage,
        "", CClientUIInterface::MSG_ERROR);
    StartShutdown();
    return false;
}

bool AbortNode(CValidationState& state, const std::string& strMessage, const std::string& userMessage="")
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

//
// Called periodically asynchronously; alerts if it smells like
// we're being fed a bad chain (blocks being generated much
// too slowly or too quickly).
//
void PartitionCheck(bool (*initialDownloadCheck)(), CCriticalSection& cs, const CBlockIndex *const &bestHeader,
                    int64_t nPowTargetSpacing)
{
    if (bestHeader == nullptr || initialDownloadCheck()) return;

    static int64_t lastAlertTime = 0;
    int64_t now = GetAdjustedTime();
    if (lastAlertTime > now-60*60*24) return; // Alert at most once per day

    const int SPAN_HOURS=4;
    const int SPAN_SECONDS=SPAN_HOURS*60*60;
    int BLOCKS_EXPECTED = SPAN_SECONDS / nPowTargetSpacing;

    boost::math::poisson_distribution<double> poisson(BLOCKS_EXPECTED);

    std::string strWarning;
    int64_t startTime = GetAdjustedTime()-SPAN_SECONDS;

    LOCK(cs);
    const CBlockIndex* i = bestHeader;
    int nBlocks = 0;
    while (i->GetBlockTime() >= startTime) {
        ++nBlocks;
        i = i->pprev;
        if (i == nullptr) return; // Ran out of chain, we must not be fully sync'ed
    }

    // How likely is it to find that many by chance?
    double p = boost::math::pdf(poisson, nBlocks);

    logInfo(Log::Bitcoin) << "PartitionCheck: Found" << nBlocks << "blocks in the last" << SPAN_HOURS << "hours";
    logInfo(Log::Bitcoin) << "PartitionCheck: likelihood:" << p;

    // Aim for one false-positive about every fifty years of normal running:
    const int FIFTY_YEARS = 50*365*24*60*60;
    double alertThreshold = 1.0 / (FIFTY_YEARS / SPAN_SECONDS);

    if (p <= alertThreshold && nBlocks < BLOCKS_EXPECTED) {
        // Many fewer blocks than expected: alert!
        strWarning = strprintf(_("WARNING: check your network connection, %d blocks received in the last %d hours (%d expected)"),
                               nBlocks, SPAN_HOURS, BLOCKS_EXPECTED);
    }
    else if (p <= alertThreshold && nBlocks > BLOCKS_EXPECTED) {
        // Many more blocks than expected: alert!
        strWarning = strprintf(_("WARNING: abnormally high number of blocks generated, %d blocks received in the last %d hours (%d expected)"),
                               nBlocks, SPAN_HOURS, BLOCKS_EXPECTED);
    }
    if (!strWarning.empty()) {
        strMiscWarning = strWarning;
        AlertNotify(strWarning, true);
        lastAlertTime = now;
        uiInterface.NotifyAlertChanged();
    }
}

bool FlushStateToDisk(CValidationState &state, FlushStateMode mode) {
    LOCK2(cs_main, cs_LastBlockFile);
    static int64_t nLastWrite = 0;
    static int64_t nLastFlush = 0;
    static int64_t nLastSetChain = 0;
    try {
    int64_t nNow = GetTimeMicros();
    // Avoid writing/flushing immediately after startup.
    if (nLastWrite == 0) {
        nLastWrite = nNow;
    }
    if (nLastFlush == 0) {
        nLastFlush = nNow;
    }
    if (nLastSetChain == 0) {
        nLastSetChain = nNow;
    }
    // It's been a while since we wrote the block index to disk. Do this frequently, so we don't need to redownload after a crash.
    bool fPeriodicWrite = mode == FLUSH_STATE_PERIODIC && nNow > nLastWrite + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
    // It's been very long since we flushed the cache. Do this infrequently, to optimize cache usage.
    bool fPeriodicFlush = mode == FLUSH_STATE_PERIODIC && nNow > nLastFlush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;
    // Combine all conditions that result in a full cache flush.
    bool fDoFullFlush = (mode == FLUSH_STATE_ALWAYS) || fPeriodicFlush;
    // Write blocks and block index to disk.
    if (fDoFullFlush || fPeriodicWrite) {
        // Depend on nMinDiskSpace to ensure we can write block index
        if (!CheckDiskSpace(0))
            return state.Error("out of disk space");
        // First make sure all block and undo data is flushed to disk.
        // Then update all block file information (which may refer to block and undo files).
        {
            std::vector<std::pair<int, const CBlockFileInfo*> > vFiles;
            vFiles.reserve(setDirtyFileInfo.size());
            for (std::set<int>::iterator it = setDirtyFileInfo.begin(); it != setDirtyFileInfo.end(); ) {
                vFiles.push_back(std::make_pair(*it, &vinfoBlockFile[*it]));
                setDirtyFileInfo.erase(it++);
            }
            std::vector<const CBlockIndex*> vBlocks;
            vBlocks.reserve(setDirtyBlockIndex.size());
            for (std::set<CBlockIndex*>::iterator it = setDirtyBlockIndex.begin(); it != setDirtyBlockIndex.end(); ) {
                vBlocks.push_back(*it);
                setDirtyBlockIndex.erase(it++);
            }
            if (Blocks::DB::instance()) { // only when we actually finished init
                if (!Blocks::DB::instance()->WriteBatchSync(vFiles, nLastBlockFile, vBlocks))
                    return AbortNode(state, "Files to write to block index database");
            }
        }
        nLastWrite = nNow;
    }
    // Flush best chain related state. This can only be done if the blocks / block index write was also done.
    if (fDoFullFlush) {
        if (!CheckDiskSpace(50000000))
            return state.Error("out of disk space");
        nLastFlush = nNow;
    }
    if (fDoFullFlush || ((mode == FLUSH_STATE_ALWAYS || mode == FLUSH_STATE_PERIODIC) && nNow > nLastSetChain + (int64_t)DATABASE_WRITE_INTERVAL * 1000000)) {
        // Update best block in wallet (so we can detect restored wallets).
        ValidationNotifier().SetBestChain(chainActive.GetLocator());
        nLastSetChain = nNow;
    }
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error while flushing: ") + e.what());
    }
    return true;
}

void FlushStateToDisk() {
    CValidationState state;
    FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
}

bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, bool fCheckPOW)
{
    // Check proof of work matches claimed amount
    if (fCheckPOW && !CheckProofOfWork(block.GetHash(), block.nBits, Params().GetConsensus()))
        return state.DoS(50, error("CheckBlockHeader(): proof of work failed"),
                         REJECT_INVALID, "high-hash");

    // Check timestamp
    if (block.GetBlockTime() > GetAdjustedTime() + 2 * 60 * 60)
        return state.Invalid(error("CheckBlockHeader(): block timestamp too far in the future"),
                             REJECT_INVALID, "time-too-new");

    return true;
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = boost::filesystem::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}

bool LoadBlockIndexDB()
{
    const CChainParams& chainparams = Params();
    if (!Blocks::DB::instance()->CacheAllBlockInfos())
        return false;

    boost::this_thread::interruption_point();

    // Calculate nChainWork
    std::vector<std::pair<int, CBlockIndex*> > vSortedByHeight = Blocks::Index::allByHeight();
    for (const PAIRTYPE(int, CBlockIndex*) &item : vSortedByHeight) {
        CBlockIndex* pindex = item.second;
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + GetBlockProof(*pindex);
        // We can link the chain of blocks for which we've received transactions at some point.
        // Pruned nodes may have deleted the block.
        if (pindex->nTx > 0) {
            if (pindex->pprev) {
                pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;
            } else {
                pindex->nChainTx = pindex->nTx;
            }
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pindex->nChainTx || pindex->pprev == nullptr))
            setBlockIndexCandidates.insert(pindex);
        if (pindex->nStatus & BLOCK_FAILED_MASK && (!pindexBestInvalid || pindex->nChainWork > pindexBestInvalid->nChainWork))
            pindexBestInvalid = pindex;
        if (pindex->pprev)
            pindex->BuildSkip();
        if (pindex->IsValid(BLOCK_VALID_TREE) && (pindexBestHeader == nullptr || CBlockIndexWorkComparator()(pindexBestHeader, pindex)))
            pindexBestHeader = pindex;
    }

    // Load block file info
    if (Blocks::DB::instance()->ReadLastBlockFile(nLastBlockFile)) {
        vinfoBlockFile.resize(nLastBlockFile + 1);
        logInfo(Log::DB) << "last block file:" << nLastBlockFile;
        for (int nFile = 0; nFile <= nLastBlockFile; nFile++) {
            Blocks::DB::instance()->ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
        }
        logInfo(Log::DB) << "last block file info:" << vinfoBlockFile[nLastBlockFile].ToString();
        for (int nFile = nLastBlockFile + 1; true; nFile++) {
            CBlockFileInfo info;
            if (Blocks::DB::instance()->ReadBlockFileInfo(nFile, info)) {
                vinfoBlockFile.push_back(info);
            } else {
                break;
            }
        }
    }

    // Check presence of blk files
    logInfo(Log::DB) << "Checking all blk files are present...";
    std::set<int> setBlkDataFiles = Blocks::Index::fileIndexes();
    for (std::set<int>::iterator it = setBlkDataFiles.begin(); it != setBlkDataFiles.end(); it++)
    {
        Streaming::ConstBuffer dataFile = Blocks::DB::instance()->loadBlockFile(*it);
        if (!dataFile.isValid())
            return false;
    }

    // Load pointer to end of best chain
    auto tip = Blocks::Index::get(g_utxo->blockId());
    chainActive.SetTip(tip);
    pindexBestHeader = tip;
    if (tip)
        logCritical(Log::Bitcoin) << "LoadBlockIndexDB: hashBestChain:" << tip->GetBlockHash()
                                 << "height:" << chainActive.Height()
                                 << "date:" << DateTimeStrFormat("%Y-%m-%d %H:%M:%S", tip->GetBlockTime())
                                 << "progress:" <<  Checkpoints::GuessVerificationProgress(chainparams.Checkpoints(), tip)
                                 << "header height:" << Blocks::DB::instance()->headerChain().Height();
    return true;
}

void UnloadBlockIndex()
{
    LOCK(cs_main);
    setBlockIndexCandidates.clear();
    chainActive.SetTip(nullptr);
    pindexBestInvalid = nullptr;
    pindexBestHeader = nullptr;
    mempool.clear();
    CTxOrphanCache::clear();
    nSyncStarted = 0;
    vinfoBlockFile.clear();
    nLastBlockFile = 0;
    nBlockSequenceId = 1;
    mapBlockSource.clear();
    mapBlocksInFlight.clear();
    nPreferredDownload = 0;
    setDirtyBlockIndex.clear();
    setDirtyFileInfo.clear();
    mapNodeState.clear();
    recentRejects.reset(nullptr);

    Blocks::Index::unload();
}

bool InitBlockIndex(const CChainParams& chainparams)
{
    // Initialize global variables that cannot be constructed at startup.
    recentRejects.reset(new CRollingBloomFilter(120000, 0.000001));

    // Check whether we're already initialized
    if (chainActive.Genesis() != nullptr)
        return true;

    logCritical(Log::Bitcoin) << "Initializing databases...";

    // Only add the genesis block if not reindexing (in which case we reuse the one already on disk)
    if (!Blocks::DB::instance()->isReindexing()) {
        CBlock &block = const_cast<CBlock&>(chainparams.GenesisBlock());

        auto *bv = Application::instance()->validation();
        auto future = bv->addBlock(FastBlock::fromOldBlock(block), Validation::SaveGoodToDisk).start();
        future.waitUntilFinished();
        if (!future.error().empty()) {
            logCritical(Log::Bitcoin) << "Failed to add the genesisblock due to:" << future.error();
            return false;
        }
        // Force a chainstate write so that when we VerifyDB in a moment, it doesn't check stale data
        CValidationState state;
        return FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
    }

    return true;
}

std::string GetWarnings(const std::string& strFor)
{
    std::string strStatusBar;
    std::string strRPC;
    std::string strGUI;

    if (!CLIENT_VERSION_IS_RELEASE) {
        strStatusBar = "This is a pre-release test build - use at your own risk - do not use for mining or merchant applications";
        strGUI = _("This is a pre-release test build - use at your own risk - do not use for mining or merchant applications");
    }

    if (GetBoolArg("-testsafemode", Settings::DefaultTestSafeMode))
        strStatusBar = strRPC = strGUI = "testsafemode enabled";

    // Misc warnings like out of disk space and clock is wrong
    if (strMiscWarning != "")
    {
        strStatusBar = strGUI = strMiscWarning;
    }

    if (strFor == "gui")
        return strGUI;
    else if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;
    assert(!"GetWarnings(): invalid parameter");
    return "error";
}








//////////////////////////////////////////////////////////////////////////////
//
// Messages
//


bool static AlreadyHave(const CInv& inv) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    switch (inv.type)
    {
    case MSG_TX: {
        auto validation = flApp->validation();
        if (validation->isRecentlyRejectedTransaction(inv.hash))
            return true;
        return validation->mempool()->exists(inv.hash);
        }
    case MSG_BLOCK:
        return Blocks::Index::exists(inv.hash);
    case MSG_DOUBLESPENDPROOF:
        return mempool.doubleSpendProofStorage()->exists(inv.hash)
                || mempool.doubleSpendProofStorage()->isRecentlyRejectedProof(inv.hash);
    }
    // Don't know what it is, just say we already got one
    return true;
}

void static ProcessGetData(CNode* pfrom, const Consensus::Params& consensusParams)
{
    logDebug(106) << pfrom->vRecvGetData.size();
    std::deque<CInv>::iterator it = pfrom->vRecvGetData.begin();

    std::vector<CInv> vNotFound;

    LOCK(cs_main);

    while (it != pfrom->vRecvGetData.end()) {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        const CInv &inv = *it;
        logDebug(106) << " + handling" << inv.ToString();
        {
            boost::this_thread::interruption_point();
            it++;

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK
                    || inv.type == MSG_THINBLOCK || inv.type == MSG_XTHINBLOCK)
            {
                bool send = false;
                auto mi = Blocks::Index::get(inv.hash);
                if (mi) {
                    if (chainActive.Contains(mi)) {
                        send = true;
                    } else {
                        static const int nOneMonth = 30 * 24 * 60 * 60;
                        // To prevent fingerprinting attacks, only send blocks outside of the active
                        // chain if they are valid, and no more than a month older (both in time, and in
                        // best equivalent proof of work) than the best header chain we know about.
                        send = mi->IsValid(BLOCK_VALID_SCRIPTS) && (pindexBestHeader != nullptr) &&
                            (pindexBestHeader->GetBlockTime() - mi->GetBlockTime() < nOneMonth) &&
                            (GetBlockProofEquivalentTime(*pindexBestHeader, *mi, *pindexBestHeader, consensusParams) < nOneMonth);
                        if (!send) {
                            logDebug(Log::Net) << "ProcessGetData ignoring request from peer"
                                                  << pfrom->GetId() << "for old block that isn't in the main chain";
                        }
                    }
                }
                // disconnect node in case we have reached the outbound limit for serving historical blocks
                // never disconnect whitelisted nodes
                static const int nOneWeek = 7 * 24 * 60 * 60; // assume > 1 week = historical
                if (send && CNode::OutboundTargetReached(true) && ( ((pindexBestHeader != nullptr) && (pindexBestHeader->GetBlockTime() - mi->GetBlockTime() > nOneWeek)) || inv.type == MSG_FILTERED_BLOCK) && !pfrom->fWhitelisted)
                {
                    logCritical(Log::Net) << "historical block serving limit reached, disconnect peer" << pfrom->GetId();

                    //disconnect node
                    pfrom->fDisconnect = true;
                    send = false;
                }
                // Pruned nodes may have deleted the block, so check whether
                // it's available before trying to send.
                if (send && (mi->nStatus & BLOCK_HAVE_DATA))
                {
                    logDebug(107) << " requested block available";
                    // Send block from disk
                    CBlock block;
                    if (!ReadBlockFromDisk(block, mi, consensusParams))
                        assert(!"cannot load block from disk");

                    bool sendFullBlock = true;

                    if (inv.type == MSG_XTHINBLOCK) {
                        CXThinBlock xThinBlock(block, pfrom->pThinBlockFilter);
                        if (!xThinBlock.collision) {
                            const int nSizeBlock = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
                            // Only send a thinblock if smaller than a regular block
                            const int nSizeThinBlock = ::GetSerializeSize(xThinBlock, SER_NETWORK, PROTOCOL_VERSION);
                            if (nSizeThinBlock < nSizeBlock) {
                                pfrom->PushMessage(NetMsgType::XTHINBLOCK, xThinBlock);
                                sendFullBlock = false;
                                logInfo(Log::ThinBlocks) << "Sent xthinblock - size:" << nSizeThinBlock << "vs block size:"
                                                         << nSizeBlock << "=> tx hashes:"  << xThinBlock.vTxHashes.size()
                                                         << "transactions:" << xThinBlock.vMissingTx.size() << "peerid"
                                                         << pfrom->id;
                            }
                        }
                    }
                    else if (inv.type == MSG_FILTERED_BLOCK)
                    {
                        LOCK(pfrom->cs_filter);
                        if (pfrom->pfilter)
                        {
                            CMerkleBlock merkleBlock(block, *pfrom->pfilter);
                            pfrom->PushMessage(NetMsgType::MERKLEBLOCK, merkleBlock);
                            // CMerkleBlock just contains hashes, so also push any transactions in the block the client did not see
                            // This avoids hurting performance by pointlessly requiring a round-trip
                            // Note that there is currently no way for a node to request any single transactions we didn't send here -
                            // they must either disconnect and retry or request the full block.
                            // Thus, the protocol spec specified allows for us to provide duplicate txn here,
                            // however we MUST always provide at least what the remote peer needs
                            typedef std::pair<unsigned int, uint256> PairType;
                            for (PairType& pair : merkleBlock.vMatchedTxn)
                                pfrom->PushMessage(NetMsgType::TX, block.vtx[pair.first]);
                            sendFullBlock = false;
                        }
                    }
                    if (sendFullBlock) // if none of the other methods were actually executed;
                         pfrom->PushMessage(NetMsgType::BLOCK, block);

                    // Trigger the peer node to send a getblocks request for the next batch of inventory
                    if (inv.hash == pfrom->hashContinue)
                    {
                        // Bypass PushInventory, this must send even if redundant,
                        // and we want it right after the last block so they don't
                        // wait for other stuff first.
                        std::vector<CInv> vInv;
                        vInv.push_back(CInv(MSG_BLOCK, chainActive.Tip()->GetBlockHash()));
                        pfrom->PushMessage(NetMsgType::INV, vInv);
                        pfrom->hashContinue.SetNull();
                    }
                }
            }
            else if (inv.IsKnownType()) {
                // Send stream from relay memory
                bool pushed = false;
                {
                    LOCK(cs_mapRelay);
                    std::map<CInv, CDataStream>::iterator mi = mapRelay.find(inv);
                    if (mi != mapRelay.end()) {
                        pfrom->PushMessage(inv.GetCommand(), (*mi).second);
                    }
                }
                if (inv.type == MSG_TX) {
                    CTransaction tx;
                    if (mempool.lookup(inv.hash, tx)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << tx;
                        pfrom->PushMessage(NetMsgType::TX, ss);
                        pushed = true;
                    }
                }
                else if (inv.type == MSG_DOUBLESPENDPROOF) {
                    DoubleSpendProof dsp = mempool.doubleSpendProofStorage()->lookup(inv.hash);
                    if (!dsp.isEmpty()) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(600);
                        ss << dsp;
                        pfrom->PushMessage(NetMsgType::DSPROOF, ss);
                        pushed = true;
                    }
                }
                if (!pushed)
                    vNotFound.push_back(inv);
            }

            // Track requests for our stuff.
            ValidationNotifier().Inventory(inv.hash);

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK
                    || inv.type == MSG_THINBLOCK || inv.type == MSG_XTHINBLOCK)
                break;
        }
    }

    pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);

    if (!vNotFound.empty()) {
        // Let the peer know that we didn't find what it asked for, so it doesn't
        // have to wait around forever. Currently only SPV clients actually care
        // about this message: it's needed when they are recursively walking the
        // dependencies of relevant unconfirmed transactions. SPV clients want to
        // do that because they want to know about (and store and rebroadcast and
        // risk analyze) the dependencies of transactions relevant to them, without
        // having to download the entire memory pool.
        pfrom->PushMessage(NetMsgType::NOTFOUND, vNotFound);
    }
}

bool static ProcessMessage(CNode* pfrom, std::string strCommand, CDataStream& vRecv, int64_t nTimeReceived)
{
    const CChainParams& chainparams = Params();
    RandAddSeedPerfmon();
    const bool fReindex = Blocks::DB::instance()->isReindexing();
    logDebug(Log::Net) << "received:" << SanitizeString(strCommand) << "bytes:" << vRecv.size() << "peer:" << pfrom->id;
    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
    {
        LogPrintf("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    const bool xthinEnabled = IsThinBlocksEnabled();

    if (strCommand == NetMsgType::VERSION)
    {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_DUPLICATE, std::string("Duplicate version message"));
            Misbehaving(pfrom->GetId(), 10);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;

        if (pfrom->nVersion < MIN_PEER_PROTO_VERSION)
        {
            // disconnect from peers older than this proto version
            logWarning(Log::Net) << "peer:" << pfrom->id << "using obsolete version" << pfrom->nVersion << "disconnecting";
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", MIN_PEER_PROTO_VERSION));
            pfrom->fDisconnect = true;
            addrman.increaseUselessness(pfrom->addr, 2);
            return false;
        }

        if (pfrom->nVersion == 10300)
            pfrom->nVersion = 300;
        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty()) {
            vRecv >> LIMITED_STRING(pfrom->strSubVer, MAX_SUBVERSION_LENGTH);
            pfrom->cleanSubVer = SanitizeString(pfrom->strSubVer);
        }
        if (!vRecv.empty())
            vRecv >> pfrom->nStartingHeight;
        if (!vRecv.empty())
            vRecv >> pfrom->fRelayTxes; // set to true after we get the first filter* message
        else
            pfrom->fRelayTxes = true;

        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1)
        {
            logCritical(Log::Net) << "connected to self at" << pfrom->addr << "disconnecting";
            pfrom->fDisconnect = true;
            return true;
        }

        pfrom->addrLocal = addrMe;
        if (pfrom->fInbound && addrMe.IsRoutable())
        {
            SeenLocal(addrMe);
        }

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            pfrom->PushVersion();

        pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

        // Potentially mark this peer as a preferred download peer.
        UpdatePreferredDownload(pfrom, State(pfrom->GetId()));

        // Change version
        pfrom->PushMessage(NetMsgType::VERACK);
        pfrom->ssSend.SetVersion(std::min(pfrom->nVersion, PROTOCOL_VERSION));

        if (!pfrom->fInbound)
        {
            // Advertise our address
            if (fListen && !IsInitialBlockDownload())
            {
                CAddress addr = GetLocalAddress(&pfrom->addr);
                if (addr.IsRoutable())
                {
                    logInfo(Log::Net) << "ProcessMessages: advertising address" << addr;
                    pfrom->PushAddress(addr);
                } else if (IsPeerAddrLocalGood(pfrom)) {
                    addr.SetIP(pfrom->addrLocal);
                    logInfo(Log::Net) << "ProcessMessages: advertising address" << addr;
                    pfrom->PushAddress(addr);
                }
            }

            // Get recent addresses
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000)
            {
                pfrom->PushMessage(NetMsgType::GETADDR);
                pfrom->fGetAddr = true;
            }
            addrman.Good(pfrom->addr);
        } else {
            if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom)
            {
                addrman.Add(addrFrom, addrFrom);
                addrman.Good(addrFrom);
            }
        }

        pfrom->fSuccessfullyConnected = true;
        logInfo(Log::Net) << "receive version message:" << pfrom->addr << pfrom->cleanSubVer << "version:"
                          << pfrom->nVersion << "blocks:" << pfrom->nStartingHeight << "id:" << pfrom->id;
        int64_t nTimeOffset = nTime - GetTime();
        pfrom->nTimeOffset = nTimeOffset;
        AddTimeData(pfrom->addr, nTimeOffset);
    }

    else if (pfrom->nVersion == 0)
    {
        // Must have a version message before anything else
        Misbehaving(pfrom->GetId(), 1);
        return false;
    }


    else if (strCommand == NetMsgType::VERACK)
    {
        pfrom->SetRecvVersion(std::min(pfrom->nVersion, PROTOCOL_VERSION));

        // Mark this node as currently connected, so we update its timestamp later.
        if (pfrom->fNetworkNode) {
            LOCK(cs_main);
            State(pfrom->GetId())->fCurrentlyConnected = true;
        }

        if (pfrom->nVersion >= SENDHEADERS_VERSION) {
            // Tell our peer we prefer to receive headers rather than inv's
            // We send this to non-NODE NETWORK peers as well, because even
            // non-NODE NETWORK peers can announce blocks (such as pruning
            // nodes)

            // BUIP010 Extreme Thinblocks: We only do inv/getdata for xthinblocks and so we must have headersfirst turned off
            if (!xthinEnabled)
                pfrom->PushMessage(NetMsgType::SENDHEADERS);
        }
    }


    else if (strCommand == NetMsgType::ADDR && (pfrom->nServices & NODE_BITCOIN_CASH))
    {
        std::vector<CAddress> vAddr;
        vRecv >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->nVersion < CADDR_TIME_VERSION && addrman.size() > 1000)
            return true;
        if (vAddr.size() > 1000)
        {
            Misbehaving(pfrom->GetId(), 20);
            return error("message addr size() = %u", vAddr.size());
        }

        // Store the new addresses
        std::vector<CAddress> vAddrOk;
        int64_t nNow = GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        for (CAddress& addr : vAddr) {
            boost::this_thread::interruption_point();

            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable())
            {
                // Relay to a limited number of other nodes
                {
                    LOCK(cs_vNodes);
                    // Use deterministic randomness to send to the same nodes for 24 hours
                    // at a time so the addrKnowns of the chosen nodes prevent repeats
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint64_t hashAddr = addr.GetHash();
                    uint256 hashRand = ArithToUint256(UintToArith256(hashSalt) ^ (hashAddr<<32) ^ ((GetTime()+hashAddr)/(24*60*60)));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    std::multimap<uint256, CNode*> mapMix;
                    for (CNode* pnode : vNodes) {
                        if (pnode->nVersion < CADDR_TIME_VERSION)
                            continue;
                        unsigned int nPointer;
                        memcpy(&nPointer, &pnode, sizeof(nPointer));
                        uint256 hashKey = ArithToUint256(UintToArith256(hashRand) ^ nPointer);
                        hashKey = Hash(BEGIN(hashKey), END(hashKey));
                        mapMix.insert(std::make_pair(hashKey, pnode));
                    }
                    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
                    for (std::multimap<uint256, CNode*>::iterator mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
                        ((*mi).second)->PushAddress(addr);
                }
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
            pfrom->fDisconnect = true;
    }

    else if (strCommand == NetMsgType::SENDHEADERS)
    {
        LOCK(cs_main);
        // BUIP010 Xtreme Thinblocks: We only do inv/getdata for xthinblocks and so we must have headersfirst turned off
        if (xthinEnabled)
            State(pfrom->GetId())->fPreferHeaders = false;
        else
            State(pfrom->GetId())->fPreferHeaders = true;
    }


    else if (strCommand == NetMsgType::INV)
    {
        std::vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ) {
            Misbehaving(pfrom->GetId(), 20);
            return error("message inv size() = %u", vInv.size());
        }

        bool fBlocksOnly = GetBoolArg("-blocksonly", Settings::DefaultBlocksOnly);

        // When catching up, avoid accepting transactions before we reach the tip, since they could get blacklisted.
        if (Blocks::DB::instance()->headerChain().Height() - chainActive.Height() > 6)
            fBlocksOnly = true;

        // Allow whitelisted peers to send data other than blocks in blocks only mode if whitelistrelay is true
        if (pfrom->fWhitelisted && GetBoolArg("-whitelistrelay", Settings::DefaultWhitelistRelay))
            fBlocksOnly = false;

        LOCK(cs_main);

        std::vector<CInv> vToFetch;
        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++) {
            const CInv &inv = vInv[nInv];
            pfrom->AddInventoryKnown(inv);

            bool fAlreadyHave = AlreadyHave(inv);
            logDebug(Log::Net) << "got inv:" << inv << (fAlreadyHave ? "have." : "new.") << "Peer:" << pfrom->id;

            if (inv.type == MSG_BLOCK) {
                UpdateBlockAvailability(pfrom->GetId(), inv.hash);
                if (!fAlreadyHave && !fReindex && !mapBlocksInFlight.count(inv.hash)) {
                    // First request the headers preceding the announced block. In the normal fully-synced
                    // case where a new block is announced that succeeds the current tip (no reorganization),
                    // there are no such headers.
                    // Secondly, and only when we are close to being synced, we request the announced block directly,
                    // to avoid an extra round-trip. Note that we must *first* ask for the headers, so by the
                    // time the block arrives, the header chain leading up to it is already validated. Not
                    // doing this will result in the received block being rejected as an orphan in case it is
                    // not a direct successor.
                    pfrom->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexBestHeader), inv.hash);
                    CNodeState *nodestate = State(pfrom->GetId());
                    if (CanDirectFetch(chainparams.GetConsensus()) &&
                        nodestate->nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
                        // BUIP010 Xtreme Thinblocks: begin section
                        CInv inv2(inv);
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        if (xthinEnabled && IsChainNearlySyncd()) {
                            if (HaveThinblockNodes() && CheckThinblockTimer(inv.hash)) {
                                // Must download a block from a ThinBlock peer
                                if (pfrom->mapThinBlocksInFlight.size() < 1 && pfrom->ThinBlockCapable()) { // We can only send one thinblock per peer at a time
                                    pfrom->mapThinBlocksInFlight[inv2.hash] = GetTime();
                                    inv2.type = MSG_XTHINBLOCK;
                                    CBloomFilter filterMemPool = createSeededBloomFilter(CTxOrphanCache::instance()->fetchTransactionIds());
                                    ss << inv2;
                                    ss << filterMemPool;
                                    pfrom->PushMessage(NetMsgType::GET_XTHIN, ss);
                                    MarkBlockAsInFlight(pfrom->GetId(), inv.hash, chainparams.GetConsensus());
                                    LogPrint("thin", "Requesting Thinblock %s from peer %s (%d)\n", inv2.hash.ToString(), pfrom->addrName.c_str(),pfrom->id);
                                }
                            }
                            else {
                                // Try to download a thinblock if possible otherwise just download a regular block
                                if (pfrom->mapThinBlocksInFlight.size() < 1 && pfrom->ThinBlockCapable()) { // We can only send one thinblock per peer at a time
                                    pfrom->mapThinBlocksInFlight[inv2.hash] = GetTime();
                                    inv2.type = MSG_XTHINBLOCK;
                                    CBloomFilter filterMemPool = createSeededBloomFilter(CTxOrphanCache::instance()->fetchTransactionIds());
                                    ss << inv2;
                                    ss << filterMemPool;
                                    pfrom->PushMessage(NetMsgType::GET_XTHIN, ss);
                                    LogPrint("thin", "Requesting Thinblock %s from peer %s (%d)\n", inv2.hash.ToString(), pfrom->addrName.c_str(),pfrom->id);
                                }
                                else {
                                    LogPrint("thin", "Requesting Regular Block %s from peer %s (%d)\n", inv2.hash.ToString(), pfrom->addrName.c_str(),pfrom->id);
                                    vToFetch.push_back(inv2);
                                }
                                MarkBlockAsInFlight(pfrom->GetId(), inv.hash, chainparams.GetConsensus());
                            }
                        }
                        else {
                            vToFetch.push_back(inv2);
                            MarkBlockAsInFlight(pfrom->GetId(), inv.hash, chainparams.GetConsensus());
                            LogPrint("thin", "Requesting Regular Block %s from peer %s (%d)\n", inv2.hash.ToString(), pfrom->addrName.c_str(),pfrom->id);
                        }
                        // BUIP010 Xtreme Thinblocks: end section
                    }
                    logDebug(Log::Net) << "getheaders" << pindexBestHeader->nHeight << inv.hash
                                    << " to peer:" << pfrom->id;
                }
            }
            else if ((inv.type == MSG_DOUBLESPENDPROOF || inv.type == MSG_TX)
                    && !fBlocksOnly && !fAlreadyHave && !fReindex) {
                pfrom->AskFor(inv);
            }

            // Track requests for our stuff
            ValidationNotifier().Inventory(inv.hash);

            if (pfrom->nSendSize > (SendBufferSize() * 2)) {
                Misbehaving(pfrom->GetId(), 50);
                return error("send buffer size() = %u", pfrom->nSendSize);
            }
        }

        if (!vToFetch.empty())
            pfrom->PushMessage(NetMsgType::GETDATA, vToFetch);
    }


    else if (strCommand == NetMsgType::GETDATA)
    {
        std::vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            Misbehaving(pfrom->GetId(), 20);
            return error("message getdata size() = %u", vInv.size());
        }

        if (vInv.size() != 1)
            logDebug(Log::Net) << "received getdata (" << vInv.size() << "invsz) peer:" << pfrom->id;

        if ((vInv.size() > 0) || (vInv.size() == 1))
            logDebug(Log::Net) << "received getdata for:" << vInv[0].ToString() << "peer:" << pfrom->id;

        pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), vInv.begin(), vInv.end());
        ProcessGetData(pfrom, chainparams.GetConsensus());
    }


    else if (strCommand == NetMsgType::GETBLOCKS)
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        // Find the last block the caller has in the main chain
        CBlockIndex* pindex = FindForkInGlobalIndex(chainActive, locator);

        // Send the rest of the chain
        if (pindex)
            pindex = chainActive.Next(pindex);
        int nLimit = 500;
        LogPrint("net", "getblocks %d to %s limit %d from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, pfrom->id);
        for (; pindex; pindex = chainActive.Next(pindex)) {
            if (pindex->GetBlockHash() == hashStop) {
                LogPrint("net", "  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                break;
            }
            pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            if (--nLimit <= 0) {
                // When this block is requested, we'll send an inv that'll
                // trigger the peer to getblocks the next batch of inventory.
                LogPrint("net", "  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
        }
    }


    else if (strCommand == NetMsgType::GETHEADERS)
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);
        if (IsInitialBlockDownload() && !pfrom->fWhitelisted) {
            logDebug(Log::Net) << "Ignoring getheaders from peer:" <<pfrom->id << "because node is in initial block download";
            return true;
        }

        CNodeState *nodestate = State(pfrom->GetId());
        CBlockIndex* pindex = nullptr;
        if (locator.IsNull())
        {
            // If locator is null, return the hashStop block
            pindex = Blocks::Index::get(hashStop);
            if (!pindex)
                return true;
        }
        else
        {
            // Find the last block the caller has in the main chain
            pindex = FindForkInGlobalIndex(chainActive, locator);
            if (pindex) {
                if (pindex->nStatus & BLOCK_FAILED_MASK) {
                    // his TIP is one we rejected. We don't like them.
                    Misbehaving(pfrom->GetId(), 100);
                    return error("peer follows a different chain.");
                }
                pindex = chainActive.Next(pindex);
            }
        }

        // we must use CBlocks, as CBlockHeaders won't include the 0x00 nTx count at the end
        std::vector<CBlock> vHeaders;
        int nLimit = MAX_HEADERS_RESULTS;
        logDebug(Log::Net) << "getheaders" << (pindex ? pindex->nHeight : -1) << "to"
                << hashStop << "from peer:" << pfrom->id;
        for (; pindex; pindex = chainActive.Next(pindex)) {
            vHeaders.push_back(pindex->GetBlockHeader());
            if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                break;
        }
        // pindex can be nullptr either if we sent chainActive.Tip() OR
        // if our peer has chainActive.Tip() (and thus we are sending an empty
        // headers message). In both cases it's safe to update
        // pindexBestHeaderSent to be our tip.
        nodestate->pindexBestHeaderSent = pindex ? pindex : chainActive.Tip();
        pfrom->PushMessage(NetMsgType::HEADERS, vHeaders);
    }


    else if (strCommand == NetMsgType::TX)
    {
        // Stop processing the transaction early if
        // We are in blocks only mode and peer is either not whitelisted or whitelistrelay is off
        if (GetBoolArg("-blocksonly", Settings::DefaultBlocksOnly) && (!pfrom->fWhitelisted || !GetBoolArg("-whitelistrelay", Settings::DefaultWhitelistRelay))) {
            LogPrint("net", "transaction sent in violation of protocol peer=%d\n", pfrom->id);
            return true;
        }

        CTransaction tx;
        vRecv >> tx;

        CInv inv(MSG_TX, tx.GetHash());
        pfrom->AddInventoryKnown(inv);
        pfrom->setAskFor.erase(inv.hash);
        {
            LOCK(cs_main);
            mapAlreadyAskedFor.erase(inv.hash);
        }

        flApp->validation()->addTransaction(Tx::fromOldTransaction(tx),
                Validation::ForwardGoodToPeers|Validation::PunishBadNode|Validation::RateLimitFreeTx, pfrom);
        CValidationState val;
        if (!FlushStateToDisk(val, FLUSH_STATE_PERIODIC))
            AbortNode(val.GetRejectReason().c_str());
    }


    else if (strCommand == NetMsgType::HEADERS && !fReindex) // Ignore headers received while importing
    {
        std::vector<CBlockHeader> headers;

        // Bypass the normal CBlock deserialization, as we don't want to risk deserializing 2000 full blocks.
        unsigned int nCount = ReadCompactSize(vRecv);
        if (nCount > MAX_HEADERS_RESULTS) {
            Misbehaving(pfrom->GetId(), 20);
            return error("headers message size = %u", nCount);
        }
        headers.resize(nCount);
        for (unsigned int n = 0; n < nCount; n++) {
            vRecv >> headers[n];
            ReadCompactSize(vRecv); // ignore tx count; assume it is 0.
        }

        if (nCount == 0) {
            // Nothing interesting. Stop asking this peers for more headers.
            return true;
        }

        auto *engine = Application::instance()->validation();
        Streaming::BufferPool pool(100 * nCount);
        std::list<Validation::Settings> futures;
        for (const CBlockHeader& header : headers) {
            auto block = FastBlock::fromOldBlock(header, &pool);
            futures.push_back(engine->addBlock(block, 0).start());
        }
        for (const Validation::Settings &future : futures) {
            future.waitHeaderFinished();
            if (!future.error().empty()) {
                logWarning(Log::Net) << "Headers have issue" << future.error();
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), Settings::DefaultBanscoreThreshold);
                return false;
            }
        }
        CBlockIndex *pindexLast = futures.back().blockIndex();
        assert(pindexLast);
        UpdateBlockAvailability(pfrom->GetId(), pindexLast->GetBlockHash());

        if (nCount == MAX_HEADERS_RESULTS) {
            // Headers message had its maximum size; the peer may have more headers.
            // TODO: optimize: if pindexLast is an ancestor of chainActive.Tip or pindexBestHeader, continue
            // from there instead.
            logDebug(Log::Net).nospace() << "more getheaders (" << pindexLast->nHeight
                << ") to end to peer=" << pfrom->id << "(startheight:" << pfrom->nStartingHeight << ")";
            pfrom->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexLast), uint256());
        }

        LOCK(cs_main);

        bool fCanDirectFetch = CanDirectFetch(chainparams.GetConsensus());
        CNodeState *nodestate = State(pfrom->GetId());
        // If this set of headers is valid and ends in a block with at least as
        // much work as our tip, download as much as possible.
        logDebug(106) << "canDirectFetch" << fCanDirectFetch << "tree" << pindexLast->IsValid(BLOCK_VALID_TREE) << "more chain work:" <<
                         (chainActive.Tip()->nChainWork <= pindexLast->nChainWork);
        if (fCanDirectFetch && pindexLast->IsValid(BLOCK_VALID_TREE) && chainActive.Tip()->nChainWork <= pindexLast->nChainWork) {
            std::vector<CBlockIndex *> vToFetch;
            CBlockIndex *pindexWalk = pindexLast;
            // Calculate all the blocks we'd need to switch to pindexLast, up to a limit.
            while (pindexWalk && !chainActive.Contains(pindexWalk) && vToFetch.size() <= MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
                logDebug(106) << "starting fetch" << pindexWalk->nHeight;
                if (!(pindexWalk->nStatus & BLOCK_HAVE_DATA) &&
                        !mapBlocksInFlight.count(pindexWalk->GetBlockHash())) {
                    // We don't have this block, and it's not yet in flight.
                    vToFetch.push_back(pindexWalk);
                }
                pindexWalk = pindexWalk->pprev;
            }
            logDebug(106) << " first block that has data;" << pindexWalk->nHeight;
            logDebug(106) << " fetch" << vToFetch;
            // If pindexWalk still isn't on our main chain, we're looking at a
            // very large reorg at a time we think we're close to caught up to
            // the main chain -- this shouldn't really happen.  Bail out on the
            // direct fetch and rely on parallel download instead.
            if (!chainActive.Contains(pindexWalk)) {
                logWarning(Log::Net) << "Large reorg, won't direct fetch to" << pindexLast->GetBlockHash() << "at height:" << pindexLast->nHeight;
            } else if (!(xthinEnabled && pfrom->nServices & NODE_XTHIN)) { // xthin based downloads are done elsewhere
                std::vector<CInv> vGetData;
                // Download as much as possible, from earliest to latest.
                BOOST_REVERSE_FOREACH(CBlockIndex *pindex, vToFetch) {
                    if (nodestate->nBlocksInFlight >= MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
                        // Can't download any more from this peer
                        break;
                    }
                    vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
                    MarkBlockAsInFlight(pfrom->GetId(), pindex->GetBlockHash(), chainparams.GetConsensus(), pindex);
                    logDebug(Log::Net) << "Requesting block" << pindex->GetBlockHash() << "from  peer:" << pfrom->id;
                }
                if (vGetData.size() > 1) {
                    logDebug(Log::Net) << "Downloading blocks toward" << pindexLast->GetBlockHash() << "height:" << pindexLast->nHeight;
                }
                if (vGetData.size() > 0) {
                    pfrom->PushMessage(NetMsgType::GETDATA, vGetData);
                }
            }
        }
    }

    // BUIP010 Xtreme Thinblocks: begin section
    else if (strCommand == NetMsgType::GET_XTHIN && !fReindex) // Ignore blocks received while importing
    {
        if (!xthinEnabled) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }
        CBloomFilter filterMemPool;
        CInv inv;
        vRecv >> inv >> filterMemPool;
        if (inv.type != MSG_XTHINBLOCK && inv.type != MSG_THINBLOCK) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return false;
        }

        LoadFilter(pfrom, &filterMemPool);
        pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), inv);
        ProcessGetData(pfrom, chainparams.GetConsensus());
    }
    else if (strCommand == NetMsgType::XTHINBLOCK  && !fReindex) // Ignore blocks received while importing
    {
        if (!xthinEnabled) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }
        CXThinBlock thinBlock;
        vRecv >> thinBlock;
        logDebug(106) << "received XThinBlock" << thinBlock.header.GetHash();

        // Send expedited ASAP
        CValidationState state;
        if (!CheckBlockHeader(thinBlock.header, state, true)) { // block header is bad
            LogPrint("thin", "Thinblock %s received with bad header from peer %s (%d)\n", thinBlock.header.GetHash().ToString(), pfrom->addrName.c_str(), pfrom->id);
            Misbehaving(pfrom->id, 20);
            return false;
        }
        else if (!IsRecentlyExpeditedAndStore(thinBlock.header.GetHash()))
            SendExpeditedBlock(thinBlock, 0, pfrom);

        CInv inv(MSG_BLOCK, thinBlock.header.GetHash());
#ifdef LOG_XTHINBLOCKS
        int nSizeThinBlock = ::GetSerializeSize(thinBlock, SER_NETWORK, PROTOCOL_VERSION);
        LogPrint("thin", "Received thinblock %s from peer %s (%d). Size %d bytes.\n", inv.hash.ToString(), pfrom->addrName.c_str(), pfrom->id, nSizeThinBlock);
#endif

        bool fAlreadyHave = false;
        // An expedited block or re-requested xthin can arrive and beat the original thin block request/response
        if (!pfrom->mapThinBlocksInFlight.count(inv.hash)) {
            LogPrint("thin", "Thinblock %s from peer %s (%d) received but we already have it\n", inv.hash.ToString(), pfrom->addrName.c_str(), pfrom->id);
            LOCK(cs_main);
            fAlreadyHave = AlreadyHave(inv); // I'll still continue processing if we don't have an accepted block yet
        }

        if (!fAlreadyHave) {
           if (thinBlock.process(pfrom))
                HandleBlockMessage(pfrom, strCommand, pfrom->thinBlock,  thinBlock.GetInv());  // clears the thin block
        }
        else {
            logDebug(106) << "  already have this xthin block";
        }
    }

    else if (strCommand == NetMsgType::XBLOCKTX && !fReindex) // handle Re-requested thinblock transactions
    {
        if (!xthinEnabled) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }
        if (pfrom->xThinBlockHashes.size() != pfrom->thinBlock.vtx.size()) { // crappy, but fast solution.
            LogPrint("thin", "Inconsistent thin block data while processing xblock-tx\n");
            return true;
        }

        CXThinBlockTx thinBlockTx;
        vRecv >> thinBlockTx;

        CInv inv(MSG_XTHINBLOCK, thinBlockTx.blockhash);
        logDebug(Log::Net) << "received blocktxs for" << inv.hash << "peer" << pfrom->id;
        if (!pfrom->mapThinBlocksInFlight.count(inv.hash)) {
            LogPrint("thin", "ThinblockTx received but it was either not requested or it was beaten by another block %s  peer=%d\n", inv.hash.ToString(), pfrom->id);
            return true;
        }

        // Create the mapMissingTx from all the supplied tx's in the xthinblock
        std::map<uint64_t, CTransaction> mapMissingTx;
        for (CTransaction tx : thinBlockTx.vMissingTx) {
            mapMissingTx[tx.GetHash().GetCheapHash()] = tx;
        }

        int count=0;
        for (size_t i = 0; i < pfrom->thinBlock.vtx.size(); ++i) {
            if (pfrom->thinBlock.vtx[i].IsNull()) {
                auto val = mapMissingTx.find(pfrom->xThinBlockHashes[i]);
                if (val != mapMissingTx.end()) {
                    pfrom->thinBlock.vtx[i] = val->second;
                    --pfrom->thinBlockWaitingForTxns;
                }
                count++;
            }
        }
        LogPrint("thin", "Got %d Re-requested txs, needed %d of them\n", thinBlockTx.vMissingTx.size(), count);

        if (pfrom->thinBlockWaitingForTxns == 0) {
            // We have all the transactions now that are in this block: try to reassemble and process.
            pfrom->thinBlockWaitingForTxns = -1;
            pfrom->AddInventoryKnown(inv);

#ifdef LOG_XTHINBLOCKS
            // for compression statistics, we have to add up the size of xthinblock and the re-requested thinBlockTx.
            int nSizeThinBlockTx = ::GetSerializeSize(thinBlockTx, SER_NETWORK, PROTOCOL_VERSION);
            int blockSize = pfrom->thinBlock.GetSerializeSize(SER_NETWORK, CBlock::CURRENT_VERSION);
            LogPrint("thin", "Reassembled thin block for %s (%d bytes). Message was %d bytes (thinblock) and %d bytes (re-requested tx), compression ratio %3.2f\n",
                     pfrom->thinBlock.GetHash().ToString(),
                     blockSize,
                     pfrom->nSizeThinBlock,
                     nSizeThinBlockTx,
                     ((float) blockSize) / ( (float) pfrom->nSizeThinBlock + (float) nSizeThinBlockTx )
                     );
#endif

            // For correctness sake, assume all came from the orphans cache
            std::vector<uint256> orphans;
            orphans.reserve(pfrom->thinBlock.vtx.size());
            for (unsigned int i = 0; i < pfrom->thinBlock.vtx.size(); i++) {
                orphans.push_back(pfrom->thinBlock.vtx[i].GetHash());
            }
            HandleBlockMessage(pfrom, strCommand, pfrom->thinBlock, inv);
            CTxOrphanCache::instance()->EraseOrphans(orphans);
        }
        else {
            LogPrint("thin", "Failed to retrieve all transactions for block\n");
        }
    }

    else if (strCommand == NetMsgType::GET_XBLOCKTX && !fReindex) // return Re-requested xthinblock transactions
    {
        if (!xthinEnabled) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }
        CXRequestThinBlockTx thinRequestBlockTx;
        vRecv >> thinRequestBlockTx;

        if (thinRequestBlockTx.setCheapHashesToRequest.empty()) { // empty request??
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }
        // We use MSG_TX here even though we refer to blockhash because we need to track
        // how many xblocktx requests we make in case of DOS
        CInv inv(MSG_TX, thinRequestBlockTx.blockhash);
        LogPrint("thin", "received get_xblocktx for %s peer=%d\n", inv.hash.ToString(), pfrom->id);

        // Check for Misbehaving and DOS
        // If they make more than 20 requests in 10 minutes then disconnect them
        {
            const uint64_t nNow = GetTime();
            if (pfrom->nGetXBlockTxLastTime <= 0)
                pfrom->nGetXBlockTxLastTime = nNow;
            pfrom->nGetXBlockTxCount *= pow(1.0 - 1.0/600.0, (double)(nNow - pfrom->nGetXBlockTxLastTime));
            pfrom->nGetXBlockTxLastTime = nNow;
            pfrom->nGetXBlockTxCount += 1;
            LogPrint("thin", "nGetXBlockTxCount is %f\n", pfrom->nGetXBlockTxCount);
            if (pfrom->nGetXBlockTxCount >= 20) {
                LogPrintf("DOS: Misbehaving - requesting too many xblocktx: %s\n", inv.hash.ToString());
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 100);  // If they exceed the limit then disconnect them
            }
        }

        CBlockIndex *index = Blocks::Index::get(inv.hash);
        if (!index) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }
        if (index->nHeight + 100 < chainActive.Height()) {
            // a node that is behind should never use this method.
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 10);
            return false;
        }
        if ((index->nStatus & BLOCK_HAVE_DATA) == 0) {
            LogPrintf("GET_XBLOCKTX requested block-data not available %s\n", inv.hash.ToString().c_str());
            return false;
        }
        CBlock block;
        const Consensus::Params& consensusParams = chainparams.GetConsensus();
        LOCK(cs_main);
        if (!ReadBlockFromDisk(block, index, consensusParams)) {
            LogPrintf("Internal error, file missing datafile %d (block: %d)\n", index->nFile, index->nHeight);
            return false;
        }

        std::vector<CTransaction> vTx;
        int todo = thinRequestBlockTx.setCheapHashesToRequest.size();
        for (size_t i = 1; i < block.vtx.size(); i++) {
            uint64_t cheapHash = block.vtx[i].GetHash().GetCheapHash();
            if (thinRequestBlockTx.setCheapHashesToRequest.count(cheapHash)) {
                vTx.push_back(block.vtx[i]);
                if (--todo == 0)
                    break;
            }
        }
        if (todo > 0) { // node send us a request for transactions which were not in the block.
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }

        pfrom->AddInventoryKnown(inv);
        CXThinBlockTx thinBlockTx(thinRequestBlockTx.blockhash, vTx);
        pfrom->PushMessage(NetMsgType::XBLOCKTX, thinBlockTx);
    }
    // BUIP010 Xtreme Thinblocks: end section


    else if (strCommand == NetMsgType::BLOCK && !fReindex) // Ignore blocks received while importing
    {
        logDebug(106) << "Received a block";
        CBlock block;
        try {
            vRecv >> block;
        } catch (std::exception &e) {
            LogPrint("net", "ProcessMessage/block failed to parse message and got error: %s\n", e.what());
            pfrom->fDisconnect = true;
            return true;
        }
        logDebug(106) << "->" << block.GetHash();

        CInv inv(MSG_BLOCK, block.GetHash());
        logDebug(Log::Net) << "received" << inv << "peer" << pfrom->id;

        pfrom->AddInventoryKnown(inv);

        // BUIP010 Extreme Thinblocks: Handle Block Message
        HandleBlockMessage(pfrom, strCommand, block, inv);
        std::vector<uint256> orphans;
        orphans.reserve(block.vtx.size());
        for (unsigned int i = 0; i < block.vtx.size(); i++) {
            orphans.push_back(block.vtx[i].GetHash());
        }
        CTxOrphanCache::instance()->EraseOrphans(orphans);
    }


    else if (strCommand == NetMsgType::GETADDR)
    {
        // This asymmetric behavior for inbound and outbound connections was introduced
        // to prevent a fingerprinting attack: an attacker can send specific fake addresses
        // to users' AddrMan and later request them by sending getaddr messages.
        // Making nodes which are behind NAT and can only make outgoing connections ignore
        // the getaddr message mitigates the attack.
        if (!pfrom->fInbound) {
            LogPrint("net", "Ignoring \"getaddr\" from outbound connection. peer=%d\n", pfrom->id);
            return true;
        }

        // Only send one GetAddr response per connection to reduce resource waste
        //  and discourage addr stamping of INV announcements.
        if (pfrom->fSentAddr) {
            LogPrint("net", "Ignoring repeated \"getaddr\". peer=%d\n", pfrom->id);
            return true;
        }
        pfrom->fSentAddr = true;

        pfrom->vAddrToSend.clear();
        std::vector<CAddress> vAddr = addrman.GetAddr();
        for (const CAddress &addr : vAddr)
            pfrom->PushAddress(addr);
    }


    else if (strCommand == NetMsgType::MEMPOOL)
    {
        if (CNode::OutboundTargetReached(false) && !pfrom->fWhitelisted)
        {
            LogPrint("net", "mempool request with bandwidth limit reached, disconnect peer=%d\n", pfrom->GetId());
            pfrom->fDisconnect = true;
            return true;
        }
        LOCK2(cs_main, pfrom->cs_filter);

        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);
        std::vector<CInv> vInv;
        for (uint256& hash : vtxid) {
            CInv inv(MSG_TX, hash);
            if (pfrom->pfilter) {
                CTransaction tx;
                bool fInMemPool = mempool.lookup(hash, tx);
                if (!fInMemPool) continue; // another thread removed since queryHashes, maybe...
                if (!pfrom->pfilter->IsRelevantAndUpdate(tx)) continue;
            }
            vInv.push_back(inv);
            if (vInv.size() == MAX_INV_SZ) {
                pfrom->PushMessage(NetMsgType::INV, vInv);
                vInv.clear();
            }
        }
        if (vInv.size() > 0)
            pfrom->PushMessage(NetMsgType::INV, vInv);
    }


    else if (strCommand == NetMsgType::PING)
    {
        if (pfrom->nVersion > BIP0031_VERSION)
        {
            uint64_t nonce = 0;
            vRecv >> nonce;
            // Echo the message back with the nonce. This allows for two useful features:
            //
            // 1) A remote node can quickly check if the connection is operational
            // 2) Remote nodes can measure the latency of the network thread. If this node
            //    is overloaded it won't respond to pings quickly and the remote node can
            //    avoid sending us more work, like chain download requests.
            //
            // The nonce stops the remote getting confused between different pings: without
            // it, if the remote node sends a ping once per second and this node takes 5
            // seconds to respond to each, the 5th ping the remote sends would appear to
            // return very quickly.
            pfrom->PushMessage(NetMsgType::PONG, nonce);
        }
    }


    else if (strCommand == NetMsgType::PONG)
    {
        int64_t pingUsecEnd = nTimeReceived;
        uint64_t nonce = 0;
        size_t nAvail = vRecv.in_avail();
        bool bPingFinished = false;
        std::string sProblem;

        if (nAvail >= sizeof(nonce)) {
            vRecv >> nonce;

            // Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
            if (pfrom->nPingNonceSent != 0) {
                if (nonce == pfrom->nPingNonceSent) {
                    // Matching pong received, this ping is no longer outstanding
                    bPingFinished = true;
                    int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                    if (pingUsecTime > 0) {
                        // Successful ping time measurement, replace previous
                        pfrom->nPingUsecTime = pingUsecTime;
                        pfrom->nMinPingUsecTime = std::min(pfrom->nMinPingUsecTime, pingUsecTime);
                    } else {
                        // This should never happen
                        sProblem = "Timing mishap";
                    }
                } else {
                    // Nonce mismatches are normal when pings are overlapping
                    sProblem = "Nonce mismatch";
                    if (nonce == 0) {
                        // This is most likely a bug in another implementation somewhere; cancel this ping
                        bPingFinished = true;
                        sProblem = "Nonce zero";
                    }
                }
            } else {
                sProblem = "Unsolicited pong without ping";
            }
        } else {
            // This is most likely a bug in another implementation somewhere; cancel this ping
            bPingFinished = true;
            sProblem = "Short payload";
        }

        if (!(sProblem.empty())) {
            LogPrint("net", "pong peer=%d: %s, %x expected, %x received, %u bytes\n",
                pfrom->id,
                sProblem,
                pfrom->nPingNonceSent,
                nonce,
                nAvail);
        }
        if (bPingFinished) {
            pfrom->nPingNonceSent = 0;
        }
    }


    else if (strCommand == NetMsgType::FILTERLOAD)
    {
        if (!GetBoolArg("-peerbloomfilters", true)) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }

        CBloomFilter filter;
        vRecv >> filter;

        if (!filter.IsWithinSizeConstraints()) {
            // There is no excuse for sending a too-large filter
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        } else {
            LOCK(pfrom->cs_filter);
            delete pfrom->pfilter;
            pfrom->pfilter = new CBloomFilter(filter);
            pfrom->pfilter->UpdateEmptyFull();
        }
        pfrom->fRelayTxes = true;
    }


    else if (strCommand == NetMsgType::FILTERADD)
    {
        if (!GetBoolArg("-peerbloomfilters", true)) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }
        std::vector<unsigned char> vData;
        vRecv >> vData;

        // Nodes must NEVER send a data item > 520 bytes (the max size for a script data object,
        // and thus, the maximum size any matched object can have) in a filteradd message
        if (vData.size() > MAX_SCRIPT_ELEMENT_SIZE) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        } else {
            LOCK(pfrom->cs_filter);
            if (pfrom->pfilter)
                pfrom->pfilter->insert(vData);
            else
                Misbehaving(pfrom->GetId(), 100);
        }
    }

    else if (strCommand == NetMsgType::FILTERCLEAR)
    {
        if (!GetBoolArg("-peerbloomfilters", true)) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }
        LOCK(pfrom->cs_filter);
        delete pfrom->pfilter;
        pfrom->pfilter = new CBloomFilter();
        pfrom->fRelayTxes = true;
    }

    else if (strCommand == NetMsgType::DSPROOF) {
        uint256 hash;
        try {
            DoubleSpendProof dsp;
            vRecv >> dsp;
            if (dsp.isEmpty())
                throw std::runtime_error("DSP empty");

            hash = dsp.createHash();
            CInv inv(MSG_DOUBLESPENDPROOF, hash);
            pfrom->setAskFor.erase(inv.hash);
            {
                LOCK(cs_main);
                mapAlreadyAskedFor.erase(hash);
            }

            switch (dsp.validate(mempool, Application::instance()->validation()->tipValidationFlags(fRequireStandard))) {
            case DoubleSpendProof::Valid: {
                const auto tx = mempool.addDoubleSpendProof(dsp);
                if (tx.size() > 0) { // added to mempool correctly, then forward to nodes.
                    ValidationNotifier().DoubleSpendFound(tx, dsp);

                    CTransaction oldTx = tx.createOldTransaction();
                    LOCK(cs_vNodes);
                    for (CNode* pnode : vNodes) {
                        if(!pnode->fRelayTxes || pnode == pfrom)
                            continue;
                        LOCK(pnode->cs_filter);
                        if (pnode->pfilter) {
                            // For nodes that we sent this Tx before, send a proof.
                            if (pnode->pfilter->IsRelevantAndUpdate(oldTx))
                                pnode->PushInventory(inv);
                        } else {
                            pnode->PushInventory(inv);
                        }
                    }
                }
                break;
            }
            case DoubleSpendProof::MissingTransction:
                logDebug(Log::Net) << "DoubleSpend Proof postponed: Missing Tx";
                mempool.doubleSpendProofStorage()->addOrphan(dsp);
                break;
            case DoubleSpendProof::MissingUTXO:
                logDebug(Log::Net) << "DoubleSpendProof rejected due to missing UTXO (outdated?)";
                return false;
            case DoubleSpendProof::Invalid:
                throw std::runtime_error("Proof didn't validate");
            default:
                assert(false);
                return false;
            }
        } catch (const std::exception &e) {
            logInfo(Log::Net) << "Failure handing double spend proof. Peer:" << pfrom->GetId() << "Reason:" << e;
            if (!hash.IsNull())
                mempool.doubleSpendProofStorage()->markProofRejected(hash);
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 10);
            return false;
        }
    }

    else if (strCommand == NetMsgType::REJECT)
    {
#ifndef NDEBUG
        try {
            std::string strMsg; unsigned char ccode; std::string strReason;
            vRecv >> LIMITED_STRING(strMsg, CMessageHeader::COMMAND_SIZE) >> ccode >> LIMITED_STRING(strReason, MAX_REJECT_MESSAGE_LENGTH);

            std::ostringstream ss;
            ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

            if (strMsg == NetMsgType::BLOCK || strMsg == NetMsgType::TX)
            {
                uint256 hash;
                vRecv >> hash;
                ss << ": hash " << hash.ToString();
            }
            LogPrint("net", "Reject %s\n", SanitizeString(ss.str()));
        } catch (const std::ios_base::failure&) {
            // Avoid feedback loops by preventing reject messages from triggering a new reject message.
            LogPrint("net", "Unparseable reject message received\n");
        }
#endif
    }

    else
    {
        // Ignore unknown commands for extensibility
        logDebug(Log::Net) << "Unknown command" << SanitizeString(strCommand) << "from peer:" << pfrom->id;
    }



    return true;
}

// requires LOCK(cs_vRecvMsg)
bool ProcessMessages(CNode* pfrom)
{
    const CChainParams& chainparams = Params();
    //if (fDebug)
    //    LogPrintf("%s(%u messages)\n", __func__, pfrom->vRecvMsg.size());

    //
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //
    bool fOk = true;

    if (!pfrom->vRecvGetData.empty())
        ProcessGetData(pfrom, chainparams.GetConsensus());

    // this maintains the order of responses
    if (!pfrom->vRecvGetData.empty()) return fOk;

    std::deque<CNetMessage>::iterator it = pfrom->vRecvMsg.begin();
    while (!pfrom->fDisconnect && it != pfrom->vRecvMsg.end()) {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        // get next message
        CNetMessage& msg = *it;

        //if (fDebug)
        //    LogPrintf("%s(message %u msgsz, %u bytes, complete:%s)\n", __func__,
        //            msg.hdr.nMessageSize, msg.vRecv.size(),
        //            msg.complete() ? "Y" : "N");

        // end, if an incomplete message is found
        if (!msg.complete())
            break;

        // at this point, any failure means we can delete the current message
        it++;

        // Scan for message start
        if (pfrom->nVersion == 0) { // uninitialized.
            if (!pfrom->fInbound // we already set isCashNode bool to right value.
                    && memcmp(msg.hdr.pchMessageStart, chainparams.magic(), MESSAGE_START_SIZE) != 0) {
                addrman.increaseUselessness(pfrom->addr);
                fOk = false;
                break;
            }

            if (memcmp(msg.hdr.pchMessageStart, chainparams.magic(), MESSAGE_START_SIZE) != 0) {
                logWarning(Log::Net) << "ProcessMessage: handshake invalid messageStart"
                                     << SanitizeString(msg.hdr.GetCommand()) << "peer:" << pfrom->id;
                addrman.increaseUselessness(pfrom->addr);
                fOk = false;
                break;
            }
            assert (memcmp(msg.hdr.pchMessageStart, Params().magic(), MESSAGE_START_SIZE) == 0);
            addrman.increaseUselessness(pfrom->addr, -1);
        }

        // Read header
        CMessageHeader& hdr = msg.hdr;
        if (!hdr.IsValid(Params().magic())) {
            logWarning(Log::Net) << "PROCESSMESSAGE: ERRORS IN HEADER" << SanitizeString(msg.hdr.GetCommand()) << "peer:" << pfrom->id;
            LOCK(cs_main);
            Misbehaving(pfrom->id, 5);
            continue;
        }
        std::string strCommand = hdr.GetCommand();

        // Message size
        unsigned int nMessageSize = hdr.nMessageSize;

        // Checksum
        CDataStream& vRecv = msg.vRecv;
        uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
        unsigned int nChecksum = ReadLE32((unsigned char*)&hash);
        if (nChecksum != hdr.nChecksum)
        {
            LogPrintf("%s(%s, %u bytes): CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n", __func__,
               SanitizeString(strCommand), nMessageSize, nChecksum, hdr.nChecksum);
            continue;
        }

        // Process message
        bool fRet = false;
        try
        {
            fRet = ProcessMessage(pfrom, strCommand, vRecv, msg.nTime);
            boost::this_thread::interruption_point();
        }
        catch (const std::ios_base::failure& e)
        {
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_MALFORMED, std::string("error parsing message"));
            if (strstr(e.what(), "end of data"))
            {
                // Allow exceptions from under-length message on vRecv
                LogPrintf("%s(%s, %u bytes): Exception '%s' caught, normally caused by a message being shorter than its stated length\n", __func__, SanitizeString(strCommand), nMessageSize, e.what());
            }
            else if (strstr(e.what(), "size too large"))
            {
                // Allow exceptions from over-long size
                LogPrintf("%s(%s, %u bytes): Exception '%s' caught\n", __func__, SanitizeString(strCommand), nMessageSize, e.what());
            }
            else
            {
                PrintExceptionContinue(&e, "ProcessMessages()");
            }
        }
        catch (const boost::thread_interrupted&) {
            throw;
        }
        catch (const std::exception& e) {
            PrintExceptionContinue(&e, "ProcessMessages()");
        } catch (...) {
            PrintExceptionContinue(nullptr, "ProcessMessages()");
        }

        if (!fRet)
            LogPrintf("%s(%s, %u bytes) FAILED peer=%d\n", __func__, SanitizeString(strCommand), nMessageSize, pfrom->id);

        break;
    }

    // In case the connection got shut down, its receive buffer was wiped
    if (!pfrom->fDisconnect)
        pfrom->vRecvMsg.erase(pfrom->vRecvMsg.begin(), it);

    return fOk;
}


bool SendMessages(CNode* pto)
{
    const bool fReindex = Blocks::DB::instance()->isReindexing();
    const Consensus::Params& consensusParams = Params().GetConsensus();
    {
        // Don't send anything until we get its version message
        if (pto->nVersion == 0)
            return true;

        //
        // Message: ping
        //
        bool pingSend = false;
        if (pto->fPingQueued) {
            // RPC ping request by user
            pingSend = true;
        }
        if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros()) {
            // Ping automatically sent as a latency probe & keepalive.
            pingSend = true;
        }
        if (pingSend) {
            uint64_t nonce = 0;
            while (nonce == 0) {
                GetRandBytes((unsigned char*)&nonce, sizeof(nonce));
            }
            pto->fPingQueued = false;
            pto->nPingUsecStart = GetTimeMicros();
            if (pto->nVersion > BIP0031_VERSION) {
                pto->nPingNonceSent = nonce;
                pto->PushMessage(NetMsgType::PING, nonce);
            } else {
                // Peer is too old to support ping command with nonce, pong will never arrive.
                pto->nPingNonceSent = 0;
                pto->PushMessage(NetMsgType::PING);
            }
        }

        TRY_LOCK(cs_main, lockMain); // Acquire cs_main for IsInitialBlockDownload() and CNodeState()
        if (!lockMain)
            return true;

        // Address refresh broadcast
        if (pindexBestHeader == nullptr)
            pindexBestHeader = chainActive.Tip();
        int64_t nNow = GetTimeMicros();
        if (!IsInitialBlockDownload() && pto->nNextLocalAddrSend < nNow) {
            AdvertiseLocal(pto);
            pto->nNextLocalAddrSend = nNow + AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL * 1000000 + rand() % 500000000;
        }

        //
        // Message: addr
        //
        if (pto->nNextAddrSend < nNow) {
            pto->nNextAddrSend = PoissonNextSend(nNow, AVG_ADDRESS_BROADCAST_INTERVAL);
            std::vector<CAddress> vAddr;
            vAddr.reserve(pto->vAddrToSend.size());
            for (const CAddress& addr : pto->vAddrToSend) {
                if (!pto->addrKnown.contains(addr.GetKey()))
                {
                    pto->addrKnown.insert(addr.GetKey());
                    vAddr.push_back(addr);
                    // receiver rejects addr messages larger than 1000
                    if (vAddr.size() >= 1000)
                    {
                        pto->PushMessage(NetMsgType::ADDR, vAddr);
                        vAddr.clear();
                    }
                }
            }
            pto->vAddrToSend.clear();
            if (!vAddr.empty())
                pto->PushMessage(NetMsgType::ADDR, vAddr);
        }

        CNodeState &state = *State(pto->GetId());
        if (state.fShouldBan) {
            if (pto->fWhitelisted)
                LogPrintf("Warning: not punishing whitelisted peer %s!\n", pto->addr.ToString());
            else {
                pto->fDisconnect = true;
                if (pto->addr.IsLocal())
                    LogPrintf("Warning: not banning local peer %s!\n", pto->addr.ToString());
                else
                {
                    CNode::Ban(pto->addr, BanReasonNodeMisbehaving);
                }
            }
            state.fShouldBan = false;
        }

        for (const CBlockReject& reject : state.rejects)
            pto->PushMessage(NetMsgType::REJECT, (std::string)NetMsgType::BLOCK, reject.chRejectCode, reject.strRejectReason, reject.hashBlock);
        state.rejects.clear();

        // Start block sync
        bool fFetch = state.fPreferredDownload || (nPreferredDownload == 0 && !pto->fClient && !pto->fOneShot); // Download if this is a nice peer, or we have no nice peers and this one might do.
        if (!state.fSyncStarted && !pto->fClient && !fReindex) {
            // Only actively request headers from small number of peers, unless we're close to today.
            if (nSyncStarted < 5 || pindexBestHeader->GetBlockTime() > GetAdjustedTime() - 24 * 60 * 60) {
                state.fSyncStarted = true;
                nSyncStarted++;
                const CBlockIndex *pindexStart = pindexBestHeader;
                /* If possible, start at the block preceding the currently
                   best known header.  This ensures that we always get a
                   non-empty list of headers back as long as the peer
                   is up-to-date.  With a non-empty response, we can initialise
                   the peer's known best block.  This wouldn't be possible
                   if we requested starting at pindexBestHeader and
                   got back an empty response.  */
                if (pindexStart->pprev)
                    pindexStart = pindexStart->pprev;
                logDebug(Log::Net) << "initial getheaders" << pindexStart->nHeight << "to peer:" << pto->id <<"startheight:" << pto->nStartingHeight;
                pto->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexStart), uint256());
            }
        }

        // Resend wallet transactions that haven't gotten in a block yet
        // Except during reindex, importing and IBD, when old wallet
        // transactions become unconfirmed and spams other nodes.
        if (!fReindex && !IsInitialBlockDownload())
        {
            ValidationNotifier().ResendWalletTransactions(Blocks::DB::instance()->headerChain().Tip()->GetBlockTime());
        }

        //
        // Try sending block announcements via headers
        //
        {
            // If we have less than MAX_BLOCKS_TO_ANNOUNCE in our
            // list of block hashes we're relaying, and our peer wants
            // headers announcements, then find the first header
            // not yet known to our peer but would connect, and send.
            // If no header would connect, or if we have too many
            // blocks, or if the peer doesn't want headers, just
            // add all to the inv queue.
            LOCK(pto->cs_inventory);
            std::vector<CBlock> vHeaders;
            bool fRevertToInv = (!state.fPreferHeaders || pto->vBlockHashesToAnnounce.size() > MAX_BLOCKS_TO_ANNOUNCE);
            CBlockIndex *pBestIndex = nullptr; // last header queued for delivery
            ProcessBlockAvailability(pto->id); // ensure pindexBestKnownBlock is up-to-date

            if (!fRevertToInv) {
                bool fFoundStartingHeader = false;
                // Try to find first header that our peer doesn't have, and
                // then send all headers past that one.  If we come across any
                // headers that aren't on chainActive, give up.
                for (const uint256 &hash : pto->vBlockHashesToAnnounce) {
                    CBlockIndex *pindex = Blocks::Index::get(hash);
                    assert(pindex);
                    if (chainActive[pindex->nHeight] != pindex) {
                        // Bail out if we reorged away from this block
                        fRevertToInv = true;
                        break;
                    }
                    if (pBestIndex != nullptr && pindex->pprev != pBestIndex) {
                        // This means that the list of blocks to announce don't
                        // connect to each other.
                        // This shouldn't really be possible to hit during
                        // regular operation (because reorgs should take us to
                        // a chain that has some block not on the prior chain,
                        // which should be caught by the prior check), but one
                        // way this could happen is by using invalidateblock /
                        // reconsiderblock repeatedly on the tip, causing it to
                        // be added multiple times to vBlockHashesToAnnounce.
                        // Robustly deal with this rare situation by reverting
                        // to an inv.
                        fRevertToInv = true;
                        break;
                    }
                    pBestIndex = pindex;
                    if (fFoundStartingHeader) {
                        // add this to the headers message
                        vHeaders.push_back(pindex->GetBlockHeader());
                    } else if (PeerHasHeader(&state, pindex)) {
                        continue; // keep looking for the first new block
                    } else if (pindex->pprev == nullptr || PeerHasHeader(&state, pindex->pprev)) {
                        // Peer doesn't have this header but they do have the prior one.
                        // Start sending headers.
                        fFoundStartingHeader = true;
                        vHeaders.push_back(pindex->GetBlockHeader());
                    } else {
                        // Peer doesn't have this header or the prior one -- nothing will
                        // connect, so bail out.
                        fRevertToInv = true;
                        break;
                    }
                }
            }
            if (fRevertToInv) {
                // If falling back to using an inv, just try to inv the tip.
                // The last entry in vBlockHashesToAnnounce was our tip at some point
                // in the past.
                if (!pto->vBlockHashesToAnnounce.empty()) {
                    const uint256 &hashToAnnounce = pto->vBlockHashesToAnnounce.back();
                    CBlockIndex *pindex = Blocks::Index::get(hashToAnnounce);
                    assert(pindex);

                    // Warn if we're announcing a block that is not on the main chain.
                    // This should be very rare and could be optimized out.
                    // Just log for now.
                    if (chainActive[pindex->nHeight] != pindex) {
                        LogPrint("net", "Announcing block %s not on main chain (tip=%s)\n",
                            hashToAnnounce.ToString(), chainActive.Tip()->GetBlockHash().ToString());
                    }

                    // If the peer announced this block to us, don't inv it back.
                    // (Since block announcements may not be via inv's, we can't solely rely on
                    // setInventoryKnown to track this.)
                    if (!PeerHasHeader(&state, pindex)) {
                        pto->PushInventory(CInv(MSG_BLOCK, hashToAnnounce));
                        LogPrint("net", "%s: sending inv peer=%d hash=%s\n", __func__,
                            pto->id, hashToAnnounce.ToString());
                    }
                }
            } else if (!vHeaders.empty()) {
                if (vHeaders.size() > 1) {
                    LogPrint("net", "%s: %u headers, range (%s, %s), to peer=%d\n", __func__,
                            vHeaders.size(),
                            vHeaders.front().GetHash().ToString(),
                            vHeaders.back().GetHash().ToString(), pto->id);
                } else {
                    LogPrint("net", "%s: sending header %s to peer=%d\n", __func__,
                            vHeaders.front().GetHash().ToString(), pto->id);
                }
                pto->PushMessage(NetMsgType::HEADERS, vHeaders);
                state.pindexBestHeaderSent = pBestIndex;
            }
            pto->vBlockHashesToAnnounce.clear();
        }

        //
        // Message: inventory
        //
        std::vector<CInv> vInv;
        std::vector<CInv> vInvWait;
        {
            bool fSendTrickle = pto->fWhitelisted;
            if (pto->nNextInvSend < nNow) {
                fSendTrickle = true;
                pto->nNextInvSend = PoissonNextSend(nNow, AVG_INVENTORY_BROADCAST_INTERVAL);
            }
            LOCK(pto->cs_inventory);
            vInv.reserve(std::min<size_t>(1000, pto->vInventoryToSend.size()));
            vInvWait.reserve(pto->vInventoryToSend.size());
            for (const CInv& inv : pto->vInventoryToSend) {
                if (inv.type == MSG_TX && pto->filterInventoryKnown.contains(inv.hash))
                    continue;

                // trickle out tx inv to protect privacy
                if (inv.type == MSG_TX && !fSendTrickle)
                {
                    // 1/4 of tx invs blast to all immediately
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint256 hashRand = ArithToUint256(UintToArith256(inv.hash) ^ UintToArith256(hashSalt));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    bool fTrickleWait = ((UintToArith256(hashRand) & 3) != 0);

                    if (fTrickleWait)
                    {
                        vInvWait.push_back(inv);
                        continue;
                    }
                }

                pto->filterInventoryKnown.insert(inv.hash);

                vInv.push_back(inv);
                if (vInv.size() >= 1000)
                {
                    pto->PushMessage(NetMsgType::INV, vInv);
                    vInv.clear();
                }
            }
            pto->vInventoryToSend = vInvWait;
        }
        if (!vInv.empty())
            pto->PushMessage(NetMsgType::INV, vInv);

        // Detect whether we're stalling
        nNow = GetTimeMicros();
        if (!pto->fDisconnect && state.nStallingSince && state.nStallingSince < nNow - 1000000 * BLOCK_STALLING_TIMEOUT) {
            // Stalling only triggers when the block download window cannot move. During normal steady state,
            // the download window should be much larger than the to-be-downloaded set of blocks, so disconnection
            // should only happen during initial block download.
            logCritical(Log::Net) << "Peer" << pto->id << "is stalling block download, disconnecting";
            pto->fDisconnect = true;
        }
        // In case there is a block that has been in flight from this peer for 2 + 0.5 * N times the block interval
        // (with N the number of peers from which we're downloading validated blocks), disconnect due to timeout.
        // We compensate for other peers to prevent killing off peers due to our own downstream link
        // being saturated. We only count validated in-flight blocks so peers can't advertise non-existing block hashes
        // to unreasonably increase our timeout.
        if (!pto->fDisconnect && state.vBlocksInFlight.size() > 0) {
            QueuedBlock &queuedBlock = state.vBlocksInFlight.front();
            int nOtherPeersWithValidatedDownloads = nPeersWithValidatedDownloads - (state.nBlocksInFlightValidHeaders > 0);
            if (nNow > state.nDownloadingSince + consensusParams.nPowTargetSpacing * (BLOCK_DOWNLOAD_TIMEOUT_BASE + BLOCK_DOWNLOAD_TIMEOUT_PER_PEER * nOtherPeersWithValidatedDownloads)) {
                logCritical(Log::Net) << "Timeout downloading block" << queuedBlock.hash << "from peer" << pto->id << "disconnecting";
                pto->fDisconnect = true;
            }
        }

        //
        // Message: getdata (blocks)
        //
        std::vector<CInv> vGetData;
        if (!pto->fDisconnect && !pto->fClient && (fFetch || !IsInitialBlockDownload()) && state.nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
            std::vector<CBlockIndex*> vToDownload;
            NodeId staller = -1;
            FindNextBlocksToDownload(pto->GetId(), MAX_BLOCKS_IN_TRANSIT_PER_PEER - state.nBlocksInFlight, vToDownload, staller);
            for (CBlockIndex *pindex : vToDownload) {
                // BUIP010 Xtreme Thinblocks: begin section
                if (IsThinBlocksEnabled() && IsChainNearlySyncd()) {
                    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                    if (HaveThinblockNodes() && CheckThinblockTimer(pindex->GetBlockHash())) {
                        // Must download a block from a ThinBlock peer
                        if (pto->mapThinBlocksInFlight.size() < 1 && pto->ThinBlockCapable()) { // We can only send one thinblock per peer at a time
                            pto->mapThinBlocksInFlight[pindex->GetBlockHash()] = GetTime();
                            CBloomFilter filterMemPool = createSeededBloomFilter(CTxOrphanCache::instance()->fetchTransactionIds());
                            ss << CInv(MSG_XTHINBLOCK, pindex->GetBlockHash());
                            ss << filterMemPool;
                            pto->PushMessage(NetMsgType::GET_XTHIN, ss);
                            MarkBlockAsInFlight(pto->GetId(), pindex->GetBlockHash(), consensusParams, pindex);
                            LogPrint("thin", "Requesting thinblock %s (%d) from peer %s (%d)\n", pindex->GetBlockHash().ToString(),
                                     pindex->nHeight, pto->addrName.c_str(), pto->id);
                        }
                    }
                    else {
                        // Try to download a thinblock if possible otherwise just download a regular block
                        if (pto->mapThinBlocksInFlight.size() < 1 && pto->ThinBlockCapable()) { // We can only send one thinblock per peer at a time
                            pto->mapThinBlocksInFlight[pindex->GetBlockHash()] = GetTime();
                            CBloomFilter filterMemPool = createSeededBloomFilter(CTxOrphanCache::instance()->fetchTransactionIds());
                            ss << CInv(MSG_XTHINBLOCK, pindex->GetBlockHash());
                            ss << filterMemPool;
                            pto->PushMessage(NetMsgType::GET_XTHIN, ss);
                            LogPrint("thin", "Requesting Thinblock %s (%d) from peer %s (%d)\n", pindex->GetBlockHash().ToString(),
                                     pindex->nHeight, pto->addrName.c_str(), pto->id);
                        }
                        else {
                            vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
                            logDebug(Log::Net) << "Requesting block" << pindex->GetBlockHash() << pindex->nHeight << "from peer" << pto->addrName << pto->id;
                        }
                        MarkBlockAsInFlight(pto->GetId(), pindex->GetBlockHash(), consensusParams, pindex);
                    }
                }
                else {
                    vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
                    MarkBlockAsInFlight(pto->GetId(), pindex->GetBlockHash(), consensusParams, pindex);
                    logDebug(Log::Net) << "Requesting block" << pindex->GetBlockHash() << pindex->nHeight << "from peer" << pto->id;
                }
                // BUIP010 Xtreme Thinblocks: end section
            }
            if (state.nBlocksInFlight == 0 && staller != -1) {
                if (State(staller)->nStallingSince == 0) {
                    State(staller)->nStallingSince = nNow;
                    logDebug(Log::Net) << "Stall started peer" << staller;
                }
            }
        }

        //
        // Message: getdata (non-blocks)
        //
        while (!pto->fDisconnect && !pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow)
        {
            const CInv& inv = (*pto->mapAskFor.begin()).second;
            if (!AlreadyHave(inv))
            {
                logDebug(Log::Net) << "Requesting" << inv << "peer:" << pto->id;
                vGetData.push_back(inv);
                if (vGetData.size() >= 1000)
                {
                    pto->PushMessage(NetMsgType::GETDATA, vGetData);
                    vGetData.clear();
                }
            } else {
                //If we're not going to ask, don't expect a response.
                pto->setAskFor.erase(inv.hash);
            }
            pto->mapAskFor.erase(pto->mapAskFor.begin());
        }
        if (!vGetData.empty())
            pto->PushMessage(NetMsgType::GETDATA, vGetData);

    }
    return true;
}

std::string CBlockFileInfo::ToString() const {
     return strprintf("CBlockFileInfo(blocks=%u, size=%u)", nBlocks, nSize);
}

void MarkIndexUnsaved(CBlockIndex *index)
{
    LOCK(cs_main);
    setDirtyBlockIndex.insert(index);
}

class CMainCleanup
{
public:
    CMainCleanup() {}
    ~CMainCleanup() {
        Blocks::Index::unload();
    }
} instance_of_cmaincleanup;
