/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2015 The Bitcoin Core developers
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

#ifndef FLOWEE_MAIN_H
#define FLOWEE_MAIN_H

#if defined(HAVE_CONFIG_H)
#include "config/flowee-config.h"
#endif

#include "amount.h"
#include "chain.h"
#include "net.h"
#include "script/script_error.h"
#include "sync.h"

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <boost/atomic.hpp>
#include <boost/unordered_map.hpp>

class CBlockIndex;
class CBloomFilter;
class CChainParams;
class CInv;
class CScriptCheck;
class CTxMemPool;
class ValidationInterface;
class CValidationState;
class UnspentOutputDatabase;

struct CNodeStateStats;
struct LockPoints;

/** The maximum size of a blk?????.dat file */
static const unsigned int MAX_BLOCKFILE_SIZE = 0x40000000; // 1024 MiB

/** -par default (number of script-checking threads, 0 = auto) */
static const int DEFAULT_SCRIPTCHECK_THREADS = 0;
/** Number of blocks that can be requested at any given time from a single peer. */
static const int MAX_BLOCKS_IN_TRANSIT_PER_PEER = 16;
/** Timeout in seconds during which a peer must stall block download progress before being disconnected. */
static const unsigned int BLOCK_STALLING_TIMEOUT = 6;
/** Number of headers sent in one getheaders result. We rely on the assumption that if a peer sends
 *  less than this number, we reached its tip. Changing this value is a protocol upgrade. */
static const unsigned int MAX_HEADERS_RESULTS = 2000;
/** Size of the "block download window": how far ahead of our current height do we fetch?
 *  Larger windows tolerate larger download speed differences between peer, but increase the potential
 *  degree of disordering of blocks on disk (which make reindexing and in the future perhaps pruning
 *  harder). We'll probably want to make this a per-peer adaptive value at some point. */
static const unsigned int BLOCK_DOWNLOAD_WINDOW = 1024;
/** Time to wait (in seconds) between writing blocks/block index to disk. */
static const unsigned int DATABASE_WRITE_INTERVAL = 60 * 60;
/** Time to wait (in seconds) between flushing chainstate to disk. */
static const unsigned int DATABASE_FLUSH_INTERVAL = 24 * 60 * 60;
/** Maximum length of reject messages. */
static const unsigned int MAX_REJECT_MESSAGE_LENGTH = 111;
/** Average delay between local address broadcasts in seconds. */
static const unsigned int AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL = 24 * 24 * 60;
/** Average delay between peer address broadcasts in seconds. */
static const unsigned int AVG_ADDRESS_BROADCAST_INTERVAL = 30;
/** Average delay between trickled inventory broadcasts in seconds.
 *  Blocks, whitelisted receivers, and a random 25% of transactions bypass this. */
static const unsigned int AVG_INVENTORY_BROADCAST_INTERVAL = 5;
/** Block download timeout base, expressed in millionths of the block interval (i.e. 10 min) */
static const int64_t BLOCK_DOWNLOAD_TIMEOUT_BASE = 1000000;
/** Additional block download timeout per parallel downloading peer (i.e. 5 min) */
static const int64_t BLOCK_DOWNLOAD_TIMEOUT_PER_PEER = 500000;

/** Maximum number of headers to announce when relaying blocks with headers message.*/
static const unsigned int MAX_BLOCKS_TO_ANNOUNCE = 8;

extern CCriticalSection cs_main;
extern CTxMemPool mempool;
extern uint64_t nLastBlockTx;
extern uint64_t nLastBlockSize;
extern const std::string strMessageMagic;
extern CWaitableCriticalSection csBestBlock;
extern CConditionVariable cvBlockChange;
extern bool fIsBareMultisigStd;
extern bool fRequireStandard;
extern unsigned int nBytesPerSigOp;
extern bool fCheckpointsEnabled;
extern CFeeRate minRelayTxFee;

// Xpress Validation: begin section
/**
 * Transactions that have already been accepted into the memory pool do not need to be
 * re-verified and can avoid having to do a second and expensive CheckInputs() when
 * processing a new block.  (Protected by cs_main)
 */
static std::set<uint256> setPreVerifiedTxHash;
/**
 * Orphans that are added to the thinblock must be verifed since they have never been
 *  accepted into the memory pool.
 */
static std::set<uint256> setUnVerifiedOrphanTxHash;
// BU - Xpress Validation: end section

/** Best header we've seen so far (used for getheaders queries' starting points). */
extern CBlockIndex *pindexBestHeader;

/** Minimum disk space required - used in CheckDiskSpace() */
static const uint64_t nMinDiskSpace = 52428800;

/** Register with a network node to receive its signals */
void RegisterNodeSignals(CNodeSignals& nodeSignals);
/** Unregister a network node */
void UnregisterNodeSignals(CNodeSignals& nodeSignals);

/** 
 * Process an incoming block. This only returns after the best known valid
 * block is made active. Note that it does not, however, guarantee that the
 * specific block passed to it has been checked for validity!
 * 
 * @param[out]  state   This may be set to an Error state if any error occurred processing it, including during validation/connection/etc of otherwise unrelated blocks during reorganisation; or it may be set to an Invalid state if pblock is itself invalid (but this is not guaranteed even when the block is checked). If you want to *possibly* get feedback on whether pblock is valid, you must also install a CValidationInterface (see validationinterface.h) - this will have its BlockChecked method called whenever *any* block completes validation.
 * @param[in]   pfrom   The node which we are receiving the block from; it is added to mapBlockSource and may be penalised if the block is invalid.
 * @param[in]   pblock  The block we want to process.
 * @param[in]   fForceProcessing Process this block even if unrequested; used for non-network block sources and whitelisted peers.
 * @param[out]  dbp     If pblock is stored to disk (or already there), this will be set to its location.
 * @return True if state.IsValid()
 */
bool ProcessNewBlock(CValidationState& state, const CChainParams& chainparams, const CNode* pfrom, const CBlock* pblock, bool fForceProcessing, CDiskBlockPos* dbp);
/** Check whether enough disk space is available for an incoming block */
bool CheckDiskSpace(uint64_t nAdditionalBytes = 0);
/** Initialize a new block tree database + block data on disk */
bool InitBlockIndex(const CChainParams& chainparams);
/** Load the block tree and coins database from disk */
bool LoadBlockIndexDB(const UnspentOutputDatabase *utxo);
/** Unload database information */
void UnloadBlockIndex();
/** Process protocol messages received from a given node */
bool ProcessMessages(CNode* pfrom);
/**
 * Send queued protocol messages to be sent to a give node.
 *
 * @param[in]   pto             The node which we are sending messages to.
 */
bool SendMessages(CNode* pto);
/** Try to detect Partition (network isolation) attacks against us */
void PartitionCheck(bool (*initialDownloadCheck)(), CCriticalSection& cs, const CBlockIndex *const &bestHeader, int64_t nPowTargetSpacing);
/** Check whether we are doing an initial block download (synchronizing from disk or network) */
bool IsInitialBlockDownload();
/** Format a string that describes several potential problems detected by the core.
 * strFor can have three values:
 * - "rpc": get critical warnings, which should put the client in safe mode if non-empty
 * - "statusbar": get all warnings
 * - "gui": get all warnings, translated (where possible) for GUI
 * This function only returns the highest priority warning of the set selected by strFor.
 */
std::string GetWarnings(const std::string& strFor);
/** Find the best known block, and make it the tip of the block chain */
CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams);

bool MarkBlockAsReceived(const uint256& hash);
bool IsBlockInFlight(const uint256 &hash);

/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats);
/** Increase a node's misbehavior score. */
void Misbehaving(NodeId nodeid, int howmuch);
/** Flush all state, indexes and buffers to disk. */
void FlushStateToDisk();

void queueRejectMessage(int peerId, const uint256 &blockHash, std::uint8_t rejectCode, const std::string &rejectReason);

class CBlockUndo;
bool UndoWriteToDisk(const CBlockUndo& blockundo, CDiskBlockPos& pos, const uint256& hashBlock, const CMessageHeader::MessageStartChars& messageStart);

/** (try to) add transaction to memory pool **/
bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState &state, const CTransaction &tx, bool fLimitFree,
                        bool* pfMissingInputs, bool fOverrideMempoolLimit=false, bool fRejectAbsurdFee=false);
void AlertNotify(const std::string& strMessage, bool fThread);

struct CNodeStateStats {
    int nMisbehavior;
    int nSyncHeight;
    int nCommonHeight;
    std::vector<int> vHeightInFlight;
};

struct CDiskTxPos : public CDiskBlockPos
{
    unsigned int nTxOffset; // after header

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CDiskBlockPos*)this);
        READWRITE(VARINT(nTxOffset));
    }

    CDiskTxPos(const CDiskBlockPos &blockIn, unsigned int nTxOffsetIn) : CDiskBlockPos(blockIn.nFile, blockIn.nPos), nTxOffset(nTxOffsetIn) {
    }

    CDiskTxPos() {
        SetNull();
    }

    void SetNull() {
        CDiskBlockPos::SetNull();
        nTxOffset = 0;
    }
};


/** Context-independent validity checks */
bool CheckTransaction(const CTransaction& tx, CValidationState& state);

/**
 * Check if transaction is final and can be included in a block with the
 * specified height and time. Consensus critical.
 */
bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime);

/**
 * Check if transaction will be final in the next block to be created.
 *
 * Calls IsFinalTx() with current block height and appropriate block time.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool CheckFinalTx(const CTransaction &tx, int flags = -1);

/**
 * Test whether the LockPoints height and time are still valid on the current chain
 */
bool TestLockPointValidity(const LockPoints* lp);

/**
 * Check if transaction is final per BIP 68 sequence numbers and can be included in a block.
 * Consensus critical. Takes as input a list of heights at which tx's inputs (in order) confirmed.
 */
bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block);

/**
 * Check if transaction will be BIP 68 final in the next block to be created.
 *
 * Simulates calling SequenceLocks() with data from the tip of the current active chain.
 * Optionally stores in LockPoints the resulting height and time calculated and the hash
 * of the block needed for calculation or skips the calculation and uses the LockPoints
 * passed in for evaluation.
 * The LockPoints should not be considered valid if CheckSequenceLocks returns false.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool CheckSequenceLocks(CTxMemPool &mp, const CTransaction &tx, int flags, LockPoints* lp = nullptr, bool useExistingLockPoints = false, CBlockIndex *tip = nullptr);

/**
 * Closure representing one script verification
 * Note that this stores references to the spending transaction 
 */
class CScriptCheck
{
private:
    CScript scriptPubKey;
    CAmount amount;
    const CTransaction *ptxTo;
    unsigned int nIn;
    unsigned int nFlags;
    bool cacheStore;
    ScriptError error;

public:
    CScriptCheck(): amount(0), ptxTo(0), nIn(0), nFlags(0), cacheStore(false), error(SCRIPT_ERR_UNKNOWN_ERROR) {}
    CScriptCheck(const CScript &outputScript, CAmount amount, const CTransaction& txToIn, unsigned int nInIn, unsigned int nFlagsIn, bool cacheIn)
        : scriptPubKey(outputScript),
        amount(amount),
        ptxTo(&txToIn),
        nIn(nInIn),
        nFlags(nFlagsIn),
        cacheStore(cacheIn),
        error(SCRIPT_ERR_UNKNOWN_ERROR) {
    }

    bool operator()();

    ScriptError GetScriptError() const { return error; }
};


/** Functions for disk access for blocks */
bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos, const Consensus::Params& consensusParams);
bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex, const Consensus::Params& consensusParams);

/** Functions for validating blocks and updating the block tree */

/** Context-independent validity checks */
bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, bool fCheckPOW = true);

/** Check a block is completely valid from start to finish (only works on top of our current best block, with cs_main held) */
bool TestBlockValidity(CValidationState& state, const CChainParams& chainparams, const CBlock& block, CBlockIndex* pindexPrev, bool fCheckPOW = true, bool fCheckMerkleRoot = true);

/**  Mark this index as 'dirty' to be saved to disk soon */
void MarkIndexUnsaved(CBlockIndex *index);

class CBlockFileInfo
{
public:
    unsigned int nBlocks;      //! number of blocks stored in file
    unsigned int nSize;        //! number of used bytes of block file
    unsigned int nUndoSize;    //! number of used bytes in the undo file

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(VARINT(nBlocks));
        READWRITE(VARINT(nSize));
        READWRITE(VARINT(nUndoSize));
    }

     void SetNull() {
         nBlocks = 0;
         nSize = 0;
         nUndoSize = 0;
     }

     CBlockFileInfo() {
         SetNull();
     }

     std::string ToString() const;

     /** update statistics (does not update nSize) */
     void AddBlock() {
         nBlocks++;
     }
};

extern CCriticalSection cs_LastBlockFile;
extern std::vector<CBlockFileInfo> vinfoBlockFile;
extern int nLastBlockFile;
extern std::set<int> setDirtyFileInfo;


/** Find the last common block between the parameter chain and a locator. */
CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator);

/** Mark a block as invalid. */
// bool InvalidateBlock(CValidationState& state, const Consensus::Params& consensusParams, CBlockIndex *pindex);

/** Remove invalidity status from a block and its descendants. */
// bool ReconsiderBlock(CBlockIndex *pindex);

/** The currently-connected chain of blocks (protected by cs_main). */
extern CChain chainActive;

class UnspentOutputDatabase;
extern UnspentOutputDatabase *g_utxo;

void LimitMempoolSize(CTxMemPool& pool, size_t limit, unsigned long age);

/** Reject codes greater or equal to this can be returned by AcceptToMemPool
 * for transactions, to signal internal conditions. They cannot and should not
 * be sent over the P2P network.
 */
static const unsigned int REJECT_INTERNAL = 0x100;
/** Too high fee. Can not be triggered by P2P transactions */
static const unsigned int REJECT_HIGHFEE = 0x100;
/** Transaction is already known (either in mempool or blockchain) */
static const unsigned int REJECT_ALREADY_KNOWN = 0x101;
/** Transaction conflicts with a transaction already known */
static const unsigned int REJECT_CONFLICT = 0x102;

enum FlushStateMode {
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};
/**
 * Update the on-disk chain state.
 * The caches and indexes are flushed depending on the mode we're called with
 * if they're too large, if it's been a while since the last write,
 * or always and in all cases if we're in prune mode and are deleting files.
 */
bool FlushStateToDisk(CValidationState &state, FlushStateMode mode);

void CheckForkWarningConditions();
void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip);

#endif
