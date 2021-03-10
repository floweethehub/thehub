/*
 * This file is part of the flowee project
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

#ifndef BLOCKVALIDATION_P_H
#define BLOCKVALIDATION_P_H

/*
 * WARNING USAGE OF THIS HEADER IS RESTRICTED.
 * This Header file is part of the private API and is meant to be used solely by the validation component.
 *
 * Usage of this API will likely mean your code will break in interesting ways in the future,
 * or even stop to compile.
 *
 * YOU HAVE BEEN WARNED!!
 */

#include "Engine.h"

#include "ValidationSettings_p.h"
#include "ValidationException.h"
#include <primitives/FastBlock.h>
#include <primitives/FastUndoBlock.h>
#include <chain.h>
#include <bloom.h>
#include <txmempool.h>

#include <boost/thread.hpp>
#include <boost/unordered_map.hpp>
#include <boost/asio/io_context_strand.hpp>

// #define ENABLE_BENCHMARKS

struct ValidationFlags {
    ValidationFlags();
    bool strictPayToScriptHash;
    bool enforceBIP34;
    bool enableValidation;
    bool scriptVerifyDerSig;
    bool scriptVerifyLockTimeVerify;
    bool scriptVerifySequenceVerify;
    bool nLocktimeVerifySequence;
    bool hf201708Active;
    bool hf201805Active;
    bool hf201811Active;
    bool hf201905Active;
    bool hf201911Active;
    bool hf202005Active;

    uint32_t scriptValidationFlags(bool requireStandard) const;

    /// based on the assumption that the index is after this Flags object, update it based on chain properties
    void updateForBlock(CBlockIndex *index);
};

// implemented in TxValidation.cpp
namespace ValidationPrivate {
struct UnspentOutput {
    CScript outputScript;
    int64_t amount = 0;
    int blockheight = 0;
    bool isCoinbase = false;
};
void validateTransactionInputs(CTransaction &tx, const std::vector<UnspentOutput> &unspents, int blockHeight,
                                      ValidationFlags flags, int64_t &fees, uint32_t &txSigops, bool &spendsCoinbase, bool requireStandard);
}

struct Output {
    Output(int index, int offsetInBlock) : index(index), offsetInBlock(offsetInBlock) {}
    uint256 txid;
    int index = -1;
    int offsetInBlock = 0;
};

class BlockValidationState : public std::enable_shared_from_this<BlockValidationState>
{
public:
    /// A bit field of validations that have succeeded so far. Or a simple BlockInvalid if one of them did not.
    enum BlockValidationStatus {
        BlockValidityUnknown = 0,

        //! Parsed just the header, checked basics.
        //! Set at successful completion of checks1NoContext()
        BlockValidHeader = 1,

        //! Block has a parent state or CBlockIndex, fully validatable leading back to genesis.
        //! This block has full data (not just a header) or is on the main-chain. Same with all its parents.
        //! When this is set it is allowed for the block to start checks2HaveParentHeaders()
        BlockValidTree = 2,

        //! Block has a valid header, parsable transactions and we did contextual checks.
        //! Implies BlockValidTree to be set.
        //! set at the successful completion of checks2HaveParentHeaders();
        BlockValidChainHeaders = 4,

        //! Parent block is accepted on the chain, allowing this block to be offered as well.
        //! When this and BlockValidChainHeaders are set the block is allowed to start updateUtxoAndStartValidation()
        BlockValidParent = 8,

        //! At least one of the items didn't pass validation.
        BlockInvalid = 0x20
    };
    BlockValidationState(const std::weak_ptr<ValidationEnginePrivate> &parent, const FastBlock &block, std::uint32_t onResultFlags = 0, int originatingNodeId = -1);
    ~BlockValidationState();

    void load();

    void checks1NoContext();
    void checks2HaveParentHeaders();
    void checkSignaturesChunk();

    void blockFailed(int punishment, const std::string &error, Validation::RejectCodes code, bool corruptionPossible = false);

    /**
     * When a block is accepted as the new chain-tip, check and schedule child-blocks that are next in line to be validated.
     */
    void signalChildren() const;

    enum RecursiveOption {
        AddFlag,
        RemoveFlag
    };

    void recursivelyMark(BlockValidationStatus value, RecursiveOption option = AddFlag);

    /// schedules a call to our BlockValidationPrivate processNewBlock()
    void finishUp();

    /// When the previous block's transactions are added to the UTXO, we start our validation.
    void updateUtxoAndStartValidation();

    /**
     * @brief calculateTxCheckChunks returns the amount of 'chunks' we split the transaction pool into for parallel validation.
     * @param[out] chunks the chunk-count.
     * @param[out] itemsPerChunk the number of transactions to be processed in a single chunk
     */
    inline void calculateTxCheckChunks(int &chunks, int &itemsPerChunk) const {
        size_t txCount = m_block.transactions().size();
        chunks = std::min<int>((txCount+9) / 10, boost::thread::hardware_concurrency());
        itemsPerChunk = std::lrint(std::ceil(txCount / static_cast<float>(chunks)));
    }

    FastBlock m_block;
    CDiskBlockPos m_blockPos;
    CBlockIndex *m_blockIndex;

    const std::uint8_t m_onResultFlags;
    std::uint8_t punishment = 100;
    bool m_ownsIndex = false;
    bool m_checkingHeader = true;
    bool m_checkPow = true;
    bool m_checkMerkleRoot = true;
    bool m_checkValidityOnly = false;
    bool m_checkTransactionValidity = true;
    ValidationFlags flags;

    const std::int32_t m_originatingNodeId;
    std::string error;
    Validation::RejectCodes errorCode = Validation::NotRejected;
    bool isCorruptionPossible = false; //< true if failure could be result of a block-corruption in-transit

    mutable std::atomic<int> m_txChunkLeftToStart;
    mutable std::atomic<int> m_txChunkLeftToFinish;
    mutable std::atomic<int> m_validationStatus;

    mutable std::atomic<std::int64_t> m_blockFees;
    mutable std::atomic<std::uint32_t> m_sigChecksCounted;

    std::vector<std::unique_ptr<std::deque<FastUndoBlock::Item> > > m_undoItems;
    std::vector<std::unique_ptr<std::deque<std::int32_t> > > m_perTxFees;

    std::weak_ptr<ValidationEnginePrivate> m_parent;
    std::weak_ptr<ValidationSettingsPrivate> m_settings;
    // These children are waiting to be notified when I reach the conclusion
    // my block is likely on the main chain since that means they might be as well.
    std::vector<std::weak_ptr<BlockValidationState> > m_chainChildren;

    //   ---------- only used when m_validateOnly is true.
    // for validateOnly style we omit changing the UTXO. As such we need to
    // allow some way to do in-block tx spending. This structure does that.
    // we map txid to a pair of ints. The first int is the output index. The second int is the tx's offset in block.
    typedef boost::unordered_map<uint256, std::deque<std::pair<int, int> >, HashShortener> UnspentMap;
    UnspentMap m_txMap;

    // when a block is being checked for validity only (not appended) we store changes
    // in this map to detect double-spends.
    typedef boost::unordered_map<uint256, std::deque<int>, HashShortener> SpentMap;
    SpentMap m_spentMap;
    //   ---------- only used when m_validateOnly is true.
};

struct MapHashShortener
{
    inline size_t operator()(const uint256& hash) const {
        return hash.GetCheapHash();
    }
};

class ValidationEnginePrivate
{
public:
    ValidationEnginePrivate(Validation::EngineType type);

    void blockHeaderValidated(std::shared_ptr<BlockValidationState> state);
    void processNewBlock(std::shared_ptr<BlockValidationState> state);
    void startOrphanWithParent(std::list<std::shared_ptr<BlockValidationState> > &adoptees, const std::shared_ptr<BlockValidationState> &state);
    void prepareChain();
    void prepareChain_priv();
    void createBlockIndexFor(const std::shared_ptr<BlockValidationState> &state);
    /// called (from strand) to speed up shutdown
    void cleanup();

    void handleFailedBlock(const std::shared_ptr<BlockValidationState> &state);

    [[noreturn]] void fatal(const char *error);

    enum ProcessingType {
        CheckingHeader,
        CheckingBlock
    };

    /// reduce blocks-in-flight counters
    void blockLanded(ProcessingType type);

    /// Find out if there are unscheduled blocks left to validate and schedule them.
    void findMoreJobs();

    inline int blocksInFlightLimit() {
        return (int(boost::thread::hardware_concurrency()));
    }

    bool disconnectTip(CBlockIndex *index, bool *userClean = nullptr, bool *error = nullptr);

    boost::asio::io_context::strand strand;
    std::atomic<bool> shuttingDown;
    std::mutex lock;
    std::condition_variable waitVariable;

    /* We have some *InFlight limits here.
     * First of all, they are only loosly controlled, not very strict.
     * So, we share a threadPool with the entire application and as such we should not overwhelm it with jobs.
     * The fact that there are two checks here is because blocksInFlight is used for blocks that end up in the sequence of
     *  * checks2HaveParentHeaders
     *  * updateUtxoAndStartValidation
     *  ** checkSignaturesChunk
     *
     * The step to go from check2 to the utxo method is serialized, meaning only one at a time is doing an utxo check.
     * This would hinder the total thoughput if we made this stop the headersInFlight additions, and as such there are
     * two counters.
     */
    std::atomic<int> headersInFlight; // indicates headers being checked, can grow upto, but not including blocksInFlightLimit()
    std::atomic<int> blocksInFlight; // indicates blocks being checked, can grow upto, but not including blocksInFlightLimit()

    CChain *blockchain;
    std::atomic<CBlockIndex*> tip; ///< Since blockchain is only usable from the strand, copy this here for cross-thread usage.
    ValidationFlags tipFlags; ///< validation flags representative of the tip.

    CTxMemPool *mempool;

    uint256 hashPrevBestCoinBase;

    std::list<std::shared_ptr<BlockValidationState> > orphanBlocks;
    typedef boost::unordered_map<uint256, std::shared_ptr<BlockValidationState>, MapHashShortener> StatesMap;
    StatesMap blocksBeingValidated;
    std::vector<std::weak_ptr<BlockValidationState> > chainTipChildren;

    std::mutex recentRejectsLock;
    CRollingBloomFilter recentTxRejects;

    std::weak_ptr<ValidationEnginePrivate> me;

    const Validation::EngineType engineType;

private:
    int lastFullBlockScheduled;
    int previousPrintedHeaderHeight = 0;
#ifdef ENABLE_BENCHMARKS

public:
    std::atomic<long> m_headerCheckTime;
    std::atomic<long> m_basicValidityChecks;
    std::atomic<long> m_contextCheckTime;
    std::atomic<long> m_utxoTime;
    std::atomic<long> m_validationTime;
    std::atomic<long> m_loadingTime;
    std::atomic<long> m_mempoolTime;
    std::atomic<long> m_walletTime;
#endif
};

#endif
