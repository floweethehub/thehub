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

#include "Engine.h"
#include <SettingsDefaults.h>
#include "BlockValidation_p.h"
#include "TxValidation_p.h"
#include "DoubleSpendProofStorage.h"
#include "ValidationException.h"
#include <consensus/consensus.h>
#include <Application.h>
#include <checkpoints.h>
#include <init.h> // for StartShutdown
#include <main.h>
#include <utiltime.h>
#include <util.h>
#include <txorphancache.h>
#include <policy/policy.h>
#include <timedata.h>
#include <UiInterface.h>
#include <Logger.h>
#include <validationinterface.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <consensus/merkle.h>
#include <streaming/BufferPool.h>
#include <server/BlocksDB.h>
#include <utxo/UnspentOutputDatabase.h>

#include <UnspentOutputData.h>
#include <fstream>

#include <streaming/MessageBuilder.h>

// #define DEBUG_BLOCK_VALIDATION
#ifdef DEBUG_BLOCK_VALIDATION
# define DEBUGBV logCritical(Log::BlockValidation)
#else
# define DEBUGBV BCH_NO_DEBUG_MACRO()
#endif

using Validation::Exception;

//---------------------------------------------------------

ValidationEnginePrivate::ValidationEnginePrivate(Validation::EngineType type)
    : strand(Application::instance()->ioService()),
    shuttingDown(false),
    headersInFlight(0),
    blocksInFlight(0),
    blockchain(nullptr),
    mempool(nullptr),
    recentTxRejects(120000, 0.000001),
    engineType(type),
    lastFullBlockScheduled(-1)
#ifdef ENABLE_BENCHMARKS
    , m_headerCheckTime(0),
    m_basicValidityChecks(0),
    m_contextCheckTime(0),
    m_utxoTime(0),
    m_validationTime(0),
    m_loadingTime(0),
    m_mempoolTime(0),
    m_walletTime(0)
#endif
{
}

/**
 * @brief All blocks offered will get back here, where we check the output of the header checks.
 * @param state the actual block and its parsing state.
 *
 * This method is called in a strand, which means we can avoid locking for our member vars
 * additionally, the state object is guarenteed (by design, not by locks) to not change any of its
 * members during this call. Except for the state atomic as further validation of transactions
 * can be happening in another thread at the same time.
 */
void ValidationEnginePrivate::blockHeaderValidated(std::shared_ptr<BlockValidationState> state)
{
    assert(strand.running_in_this_thread());
    assert(state->m_blockIndex == nullptr);
    if (state->m_block.size() < 80) {// malformed block, without header we can't report issues, just return
        state->error = "Malformed block (too short)";
        return;
    }

    struct RAII {
        RAII(const std::shared_ptr<BlockValidationState> &state)
            : m_state(state),
              finished(true) { }

        ~RAII() {
            std::shared_ptr<ValidationSettingsPrivate> settingsPriv = m_state->m_settings.lock();
            if (settingsPriv) {
                if (!error.empty())
                    settingsPriv->error = error;
                if (m_state->m_ownsIndex) {
                    settingsPriv->blockHash = m_state->m_block.createHash();
                    if (!m_state->m_blockIndex->phashBlock) {
                        // make sure a non-global index still has a working blockhash pointer
                        m_state->m_blockIndex->phashBlock = &settingsPriv->blockHash;
                    }
                } else {
                    // destructor of state will not delete index, so we can safely deref
                    settingsPriv->state.reset();
                }
                settingsPriv->setBlockIndex(m_state->m_blockIndex);
                if (finished)
                    settingsPriv->markFinished();
            }
        }

        std::shared_ptr<BlockValidationState> m_state;
        bool finished;
        std::string error;
    };
    RAII raii(state);

    if (shuttingDown)
        return;
    const bool hasFailed = state->m_validationStatus.load() & BlockValidationState::BlockInvalid;
    const FastBlock &block = state->m_block;
    assert(block.size() >= 80);
    const uint256 hash = block.createHash();

    DEBUGBV << hash << "Parent:" << state->m_block.previousBlockId() << (state->m_block.isFullBlock() ? "":"[header]");
    if (!hasFailed) {
        auto iter = blocksBeingValidated.find(hash);
        if (iter != blocksBeingValidated.end()) {
            // we are already validating the block. And since the merkle root was found OK, this means
            // that its a duplicate.
            state->m_blockIndex = iter->second->m_blockIndex;
            DEBUGBV << "  + already in the blocksBeingValidated, ignoring";
            return;
        }
    }

    // check past work.
    CBlockIndex *index = nullptr;
    if (state->m_checkMerkleRoot) // We have no proper block-hash if we don't have a merkle-root.
        index = Blocks::Index::get(hash);
    if (index) { // we already parsed it...
        state->m_blockIndex = index;
        state->m_ownsIndex = false;
        DEBUGBV << "  already known at height:" << index->nHeight;
        DEBUGBV << "  current height:" << blockchain->Height();
        if (index->nStatus & BLOCK_FAILED_MASK) {
            DEBUGBV << "  Block failed previously. Ignoring";
            raii.error = "Block failed previously, not rechecking";
            return;
        }
        DEBUGBV << "  pprev:" << index->pprev;
        if (index->pprev)
            DEBUGBV << "  pprev has completed:" << index->pprev->IsValid(BLOCK_VALID_TRANSACTIONS);
        assert(index);
        assert(index->phashBlock);
        if (blockchain->Contains(index)) { // its already in the chain...
            DEBUGBV << "  Already in the chain. Exiting";
            return;
        }
        if (index->pprev) {
            auto miPrev = blocksBeingValidated.find(block.previousBlockId());
            if (miPrev != blocksBeingValidated.end()) {
                miPrev->second->m_chainChildren.push_back(state);
                DEBUGBV << "  + adding me to parents chain-children" << miPrev->second->m_block.createHash();
            } else if (index->pprev->IsValid(BLOCK_VALID_TRANSACTIONS)) {
                state->m_validationStatus.fetch_or(BlockValidationState::BlockValidParent);
                DEBUGBV << "  Setting BlockValidParent";
            }
        }
        if (index->nHeight == 0) {
            state->m_validationStatus.fetch_or(BlockValidationState::BlockValidParent);
        }
    }

    createBlockIndexFor(state);
    index = state->m_blockIndex;
    if (hasFailed || index->nStatus & BLOCK_FAILED_MASK) {
        logInfo(Log::BlockValidation) << "Block" << index->nHeight << hash << "rejected with error:" << state->error;
        if (!state->m_checkValidityOnly && state->m_checkMerkleRoot)
            handleFailedBlock(state);
        return;
    }

    state->flags = tipFlags;
    const bool receivedFromPeer = state->m_originatingNodeId  >= 0;
    bool wasRequested = false;
    if (receivedFromPeer) {
        LOCK(cs_main);
        wasRequested = IsBlockInFlight(hash);
    }

    if (index->nHeight == -1) { // is an orphan for now.
        if (state->m_checkValidityOnly) {
            state->blockFailed(100, "Block is an orphan, can't check", Validation::RejectInternal);
        } else {
            orphanBlocks.push_back(state);
            blockLanded(ValidationEnginePrivate::CheckingHeader); // a block is no longer in flight if its in the orphans cache
            DEBUGBV << " adding it to orphan blocks" << orphanBlocks.size() << "headers in flight now;" << headersInFlight.load();
            raii.finished = false;
        }
        return;
    }

    // recursively check all orphans to see if the addition of our new block attached them to the genesis.
    std::list<std::shared_ptr<BlockValidationState> > adoptees;
    if (!state->m_checkValidityOnly)
        startOrphanWithParent(adoptees, state);
    DEBUGBV << "  Found" << adoptees.size() << "adoptees";
    if (adoptees.size())
        headersInFlight.fetch_add(static_cast<int>(adoptees.size()));
#ifdef DEBUG_BLOCK_VALIDATION
    for (auto state : adoptees) {
        assert(state->m_checkingHeader);
    }
#endif

    CBlockIndex *currentHeaderTip = Blocks::DB::instance()->headerChain().Tip();
    adoptees.insert(adoptees.begin(), state);
    const auto &cpMap = Params().Checkpoints().mapCheckpoints;
    for (auto item : adoptees) {
        if (item->m_checkValidityOnly)
            continue;
        if (item->m_ownsIndex) {
            item->m_ownsIndex = false; // the CBlockIndex is now owned by the Blocks::DB
            item->m_blockIndex->RaiseValidity(BLOCK_VALID_TREE);
            item->m_blockIndex->phashBlock = Blocks::Index::insert(item->m_block.createHash(), item->m_blockIndex);
            MarkIndexUnsaved(item->m_blockIndex); // Save it to the DB.
        }
        // check checkpoints. If we have the right height but not the hash, fail block
        for (auto iter = cpMap.begin(); iter != cpMap.end(); ++iter) {
            if (iter->first == item->m_blockIndex->nHeight && iter->second != item->m_blockIndex->GetBlockHash()) {
                logCritical(Log::BlockValidation) << "Failing block due to checkpoint" << item->m_blockIndex->nHeight
                                                  << item->m_blockIndex->GetBlockHash();
                item->m_blockIndex->nStatus |= BLOCK_FAILED_VALID;
                raii.error = "Failed due to checkpoint";
                break;
            }
        }

        assert(item->m_blockIndex->pprev || item->m_blockIndex->nHeight == 0);
        Blocks::DB::instance()->appendHeader(item->m_blockIndex);
        DEBUGBV << "appendHeader block at height:" << item->m_blockIndex->nHeight;

        if (item->m_block.isFullBlock()) {
            assert(!item->m_block.transactions().empty()); // assume we already had findTransactions called before here.
            item->m_blockIndex->nTx = static_cast<std::uint32_t>(item->m_block.transactions().size());
            try { // Write block to history file
                if ((item->m_blockIndex->nStatus & BLOCK_HAVE_DATA) == 0 && item->m_onResultFlags & Validation::SaveGoodToDisk) {
                    item->m_block = Blocks::DB::instance()->writeBlock(item->m_block, item->m_blockPos);
                }
                if (!item->m_blockPos.IsNull()) {
                    item->m_blockIndex->nDataPos = item->m_blockPos.nPos;
                    item->m_blockIndex->nFile = item->m_blockPos.nFile;
                    item->m_blockIndex->nStatus |= BLOCK_HAVE_DATA;
                }
            } catch (const std::exception &e) {
                fatal(e.what());
            }
        }
    }

    CBlockIndex *prevTip = Blocks::DB::instance()->headerChain().Tip();
    assert(prevTip);
    if (currentHeaderTip != prevTip) {
        const CBlockIndex *chainTip = tip;
        const bool farBehind = chainTip && chainTip->nHeight - 1008 < prevTip->nHeight;
        if (!farBehind || previousPrintedHeaderHeight + 1000 < prevTip->nHeight) {
            logCritical(Log::BlockValidation).nospace() << "new best header=" << *prevTip->phashBlock << " height=" << prevTip->nHeight << " orphans=" << orphanBlocks.size();
            previousPrintedHeaderHeight = prevTip->nHeight;
        }
    }

    if (currentHeaderTip && !Blocks::DB::instance()->headerChain().Contains(currentHeaderTip)) { // re-org happened in headers.
        logInfo(Log::BlockValidation) << "Header-reorg detected. height=" << prevTip->nHeight <<
                                         "Old-tip" << *currentHeaderTip->phashBlock << "@" << currentHeaderTip->nHeight;
        int reorgSize = 0;
        if (!Blocks::DB::instance()->headerChain().Contains(blockchain->Tip())) {
            // the reorg removes blocks from our validated chain!
            // Now see how big a reorg we are talking...
            auto commonAncestor = Blocks::Index::lastCommonAncestor(currentHeaderTip, blockchain->Tip());
            reorgSize = 1 + blockchain->Height() - commonAncestor->nHeight;
        }
        DEBUGBV << "  + reorgSize" << reorgSize;
        DEBUGBV << "  + validation-tip" << blockchain->Height() << blockchain->Tip()->GetBlockHash();

        if (reorgSize > 6 && Params().NetworkIDString() != CBaseChainParams::REGTEST) { // reorgs are fine on REGTEST
            logCritical(Log::BlockValidation).nospace() << "Reorg larger than 6 blocks detected (" << reorgSize
                                                        << "), this needs manual intervention.";
            logCritical(Log::BlockValidation) << "  Use invalidateblock and reconsiderblock methods to change chain.";
        } else if (reorgSize > 0) {
            prepareChain();
            lastFullBlockScheduled = -1;
        }
    }

    const int diff = index->nHeight - blockchain->Height();
    if (diff <= blocksInFlightLimit()) { // if block is recent, then continue immediately.
        bool forward = false;
        bool first = true;
        // adoptees are sorted
        for (auto iter = adoptees.cbegin(); iter != adoptees.cend(); ++iter) {
            const std::shared_ptr<BlockValidationState>&item = *iter;
            if (!item->m_checkValidityOnly && !Blocks::DB::instance()->headerChain().Contains(item->m_blockIndex))
                continue;
            if (first) {
                first = false;
                // Check the first blocks BlockValidTree by checking its parents are all Ok.
                assert(item->m_blockIndex);
                if (item->m_blockIndex->nHeight <= 1 || blockchain->Contains(item->m_blockIndex->pprev)) {
                    forward = true;
                } else {
                    auto iter = blocksBeingValidated.find(item->m_blockIndex->pprev->GetBlockHash());
                    if (iter != blocksBeingValidated.end()) {
                        if (iter->second->m_validationStatus.load() & BlockValidationState::BlockValidTree) {
                            forward = true;
                        }
                    }
                }
            }
            forward = forward && item->m_block.isFullBlock();

            if (forward) {
                if (!item->m_checkValidityOnly) {
                    DEBUGBV << "moving block on to checks2. Block at height:" << item->m_blockIndex->nHeight;
                    blocksBeingValidated.insert(std::make_pair(item->m_block.createHash(), item));
                    lastFullBlockScheduled = std::max(lastFullBlockScheduled, item->m_blockIndex->nHeight);
                }

                item->m_validationStatus.fetch_or(BlockValidationState::BlockValidTree);
                Application::instance()->ioService().post(std::bind(&BlockValidationState::checks2HaveParentHeaders, item));
            }
        }
        raii.finished = !forward;
    }
}

void ValidationEnginePrivate::createBlockIndexFor(const std::shared_ptr<BlockValidationState> &state)
{
    DEBUGBV << state->m_block.createHash();
    if (state->m_blockIndex)
        return;
    const FastBlock &block = state->m_block;
    CBlockIndex *index = new CBlockIndex();
    state->m_blockIndex = index;
    state->m_ownsIndex = true;
    index->nHeight = -1;
    index->nVersion = block.blockVersion();
    index->hashMerkleRoot = block.merkleRoot();
    index->nBits = block.bits();
    index->nTime = block.timestamp();
    index->nNonce = block.nonce();
    index->nFile = state->m_blockPos.nFile;
    index->nStatus = BLOCK_VALID_HEADER;
    if (!state->m_blockPos.IsNull()) // likely found during reindex
        index->nStatus |= BLOCK_HAVE_DATA;

    auto miPrev = blocksBeingValidated.find(block.previousBlockId());
    bool hasKnownParent = miPrev != blocksBeingValidated.end();
    if (hasKnownParent) {
        index->pprev = miPrev->second->m_blockIndex;
        miPrev->second->m_chainChildren.push_back(state);
    } else {
        index->pprev = Blocks::Index::get(block.previousBlockId());
        hasKnownParent = index->pprev;
        if (hasKnownParent) {
            if (index->pprev->IsValid(BLOCK_VALID_TRANSACTIONS))
                state->m_validationStatus.fetch_or(BlockValidationState::BlockValidParent);
        } else {
            for (auto *headersTip : Blocks::DB::instance()->headerChainTips()) {
                if (headersTip->GetBlockHash() == block.previousBlockId()) {
                    index->pprev = headersTip;
                    break;
                }
            }
        }
    }
    if (index->pprev && index->pprev->nHeight != -1) {
        index->nHeight = index->pprev->nHeight + 1;
        index->nChainWork = index->pprev->nChainWork + GetBlockProof(*index);
        index->BuildSkip();
        DEBUGBV << "   block" << index->nHeight << "has pprev";
        if (index->pprev->nStatus & BLOCK_FAILED_MASK) {
            DEBUGBV << "      + Which failed!";
            index->nStatus |= BLOCK_FAILED_CHILD;
            state->blockFailed(10, "bad-parent", Validation::RejectInvalid);
        }
    }
    else if (index->pprev == nullptr && block.createHash() == Params().GetConsensus().hashGenesisBlock) {
        index->nHeight = 0;
        state->m_validationStatus.fetch_or(BlockValidationState::BlockValidParent);
        DEBUGBV.nospace() << "   is genesis block (height=" << index->nHeight << ")";
    }
}

/**
 * This method is called when the validation engine is in the process of shutting down.
 * The validation engine has shared pointers to the State objects that are being validated,
 * clearing those will causse the validation to stop, which is the wanted effect.
 * The settings objects also have a shared pointer to the State objects, so we make all
 * of those error out in order to entice other parts of the app to also delete those Settings
 * objects, which will delete the State objects and then when all State objects are deleted
 * will the BlockValidationPrivate finally be deleted too.
 */
void ValidationEnginePrivate::cleanup()
{
    assert(strand.running_in_this_thread());
    assert(shuttingDown);
    auto iter = orphanBlocks.begin();
    while (iter != orphanBlocks.end()) {
        const std::shared_ptr<BlockValidationState> &orphan = *iter;
        auto settings = orphan->m_settings.lock();
        if (settings) {
            settings->error = "shutdown";
            settings->markFinished();
        }
        ++iter;
    }
    orphanBlocks.clear();
    auto iter2 = blocksBeingValidated.begin();
    while (iter2 != blocksBeingValidated.end()) {
        const std::shared_ptr<BlockValidationState> &block = iter2->second;
        auto settings = block->m_settings.lock();
        if (settings) {
            settings->error = "shutdown";
            settings->markFinished();
        }
        ++iter2;
    }
    blocksBeingValidated.clear();
    std::unique_lock<decltype(lock)> waitLock(lock);
    waitVariable.notify_all();
}

/**
 * @brief We have a block that has tracable ancestry to our genesis. We start processing it.
 * This first starts by finding all the orphans that now can be de-orphaned because the block
 * might be their parent.
 *
 * Additionally, we now can look at the POW to see how this block relates to the main-chain.
 * @param state the block.
 */
void ValidationEnginePrivate::startOrphanWithParent(std::list<std::shared_ptr<BlockValidationState> > &adoptedItems, const std::shared_ptr<BlockValidationState> &state)
{
    assert(strand.running_in_this_thread());
    std::list<std::shared_ptr<BlockValidationState> > parents;
    parents.push_back(state); // we start with the method-argument, we replace it in each loop in the do{}while with new parents.
    do {
        std::list<std::shared_ptr<BlockValidationState> > younglings; // adoptees from the newest generation
        auto iter = orphanBlocks.begin();
        while (iter != orphanBlocks.end()) {
            std::shared_ptr<BlockValidationState> orphan = *iter;
            bool match = false;
            for (const auto &parent : parents) {
                if (parent->m_block.createHash() == orphan->m_block.previousBlockId()) {
                    // we found a new child of one of the recently found parents.
                    match = true;

                    bool alreadyThere = false;
                    for (auto child : parent->m_chainChildren) {
                        if (child.lock() == orphan) {
                            alreadyThere = true;
                            break;
                        }
                    }
                    if (!alreadyThere)
                        parent->m_chainChildren.push_back(std::weak_ptr<BlockValidationState>(orphan));

                    CBlockIndex *index = orphan->m_blockIndex;
                    index->pprev = parent->m_blockIndex;
                    index->nHeight = parent->m_blockIndex->nHeight + 1;
                    index->nChainWork = parent->m_blockIndex->nChainWork + GetBlockProof(*index);
                    index->BuildSkip();
                    adoptedItems.push_back(orphan);
                    younglings.push_back(orphan);
                    break;
                }
            }
            if (match)
                iter = orphanBlocks.erase(iter);
            else
                ++iter;
        }
        parents = younglings;
    } while (!parents.empty());
}

/*
 * When a block gets passed to this method we know the block is fully validated for
 * correctness, and so are all of the parent blocks.
 */
void ValidationEnginePrivate::processNewBlock(std::shared_ptr<BlockValidationState> state)
{
    assert(strand.running_in_this_thread());
    if (shuttingDown)
        return;
    if (state->m_blockIndex == nullptr) // already handled.
        return;

    struct RAII {
        uint256 m_hash;
        ValidationEnginePrivate *m_parent;
        std::shared_ptr<ValidationSettingsPrivate> m_priv;

        RAII(ValidationEnginePrivate *parent, const std::shared_ptr<BlockValidationState> &state)
            : m_hash(state->m_block.createHash()), m_parent(parent), m_priv(state->m_settings.lock()) { }
        ~RAII() {
            m_parent->blocksBeingValidated.erase(m_hash);
            if (m_priv)
                m_priv->markFinished();
        }
    };
    RAII raii(this, state);

    if (state->m_checkValidityOnly)
        return;

    CBlockIndex *index = state->m_blockIndex;
    const uint256 hash = state->m_block.createHash();
    DEBUGBV << hash.ToString() << state->m_blockIndex->nHeight;
    DEBUGBV << "   chain:" << blockchain->Height();

    assert(blockchain->Height() == -1 || index->nChainWork >= blockchain->Tip()->nChainWork); // the new block has more POW.

    const bool blockValid = (state->m_validationStatus.load() & BlockValidationState::BlockInvalid) == 0;
    if (!blockValid) {
        mempool->utxo()->rollback();
        logInfo(Log::BlockValidation) << " block not valid" << index->nHeight << state->m_block.createHash() << "chain-height:" << blockchain->Height();
    }
    const bool farBehind = Blocks::DB::instance()->headerChain().Height() - blockchain->Height() > 144; // catching up

    const bool isNextChainTip = index->nHeight == blockchain->Height() + 1; // If a parent was rejected for some reason, this is false
    bool addToChain = isNextChainTip && blockValid && Blocks::DB::instance()->headerChain().Contains(index);
    try {
        if (!isNextChainTip)
            index->nStatus |= BLOCK_FAILED_CHILD;
        if (addToChain) {
            DEBUGBV << "UTXO best block is" << mempool->utxo()->blockId() << "my parent is" <<  state->m_block.previousBlockId();
            if (mempool->utxo()->blockId() != state->m_block.previousBlockId())
                throw Exception("UnspentOutput DB inconsistent!");

            index->nChainTx = index->nTx + (index->pprev ? index->pprev->nChainTx : 0); // pprev is only null if this is the genesisblock.

            index->RaiseValidity(BLOCK_VALID_CHAIN);

            if (index->nHeight == 0) { // is genesis block
                mempool->utxo()->blockFinished(index->nHeight, hash);
                blockchain->SetTip(index);
                index->RaiseValidity(BLOCK_VALID_SCRIPTS); // done
                state->signalChildren();
            } else {
                const uint64_t maxSigOps = Policy::blockSigOpAcceptLimit(state->m_block.size());
                if (state->m_sigOpsCounted > maxSigOps)
                    throw Exception("bad-blk-sigops");

                CBlock block = state->m_block.createOldBlock();
                if (state->flags.enableValidation) {
                    CAmount blockReward = state->m_blockFees.load() + GetBlockSubsidy(index->nHeight, Params().GetConsensus());
                    if (block.vtx[0].GetValueOut() > blockReward)
                        throw Exception("bad-cb-amount");
                }

                assert(index->nFile >= 0); // we need the block to have been saved
                Streaming::BufferPool pool;
                UndoBlockBuilder undoBlock(hash, &pool);
                for (auto chunk : state->m_undoItems) {
                    if (chunk) undoBlock.append(*chunk);
                }
                Blocks::DB::instance()->writeUndoBlock(undoBlock, index->nFile, &index->nUndoPos);
                index->nStatus |= BLOCK_HAVE_UNDO;
#ifdef ENABLE_BENCHMARKS
                int64_t end, start = GetTimeMicros();
#endif
                mempool->utxo()->blockFinished(index->nHeight, hash);
#ifdef ENABLE_BENCHMARKS
                end = GetTimeMicros();
                m_utxoTime.fetch_add(end - start);
                start = end;
#endif

                std::list<CTransaction> txConflicted;
                mempool->removeForBlock(block.vtx, txConflicted);
                index->RaiseValidity(BLOCK_VALID_SCRIPTS); // done
                state->signalChildren(); // start tx-validation of next one.

                blockchain->SetTip(index);
                tip.store(index);
                mempool->AddTransactionsUpdated(1);
                mempool->doubleSpendProofStorage()->newBlockFound();
                cvBlockChange.notify_all();
#ifdef ENABLE_BENCHMARKS
                end = GetTimeMicros();
                m_mempoolTime.fetch_add(end - start);
                start = end;
#endif
                if (!farBehind) {
                    // ^ The Hub doesn't accept transactions on IBD, so avoid doing unneeded work.
                    std::lock_guard<std::mutex> rejects(recentRejectsLock);
                    recentTxRejects.clear();
                }

                // Tell wallet about transactions that went from mempool to conflicted:
                for (const CTransaction &tx : txConflicted) {
                    ValidationNotifier().SyncTransaction(tx);
                    ValidationNotifier().SyncTx(Tx::fromOldTransaction(tx, &pool));
                }
                ValidationNotifier().SyncAllTransactionsInBlock(state->m_block, index); // ... and about transactions that got confirmed:
                ValidationNotifier().SyncAllTransactionsInBlock(&block);

#ifdef ENABLE_BENCHMARKS
                end = GetTimeMicros();
                m_walletTime.fetch_add(end - start);
#endif
            }
        } else {
            logDebug(Log::BlockValidation) << "Not appending: isNextChainTip" << isNextChainTip << "blockValid:" << blockValid << "addToChain" << addToChain;
        }
    } catch (const Exception &e) {
        state->blockFailed(100, e.what(), e.rejectCode(), e.corruptionPossible());
        addToChain = false;
    }

    if (!blockValid) {
        logCritical(Log::BlockValidation) << "block failed validation" << state->error << index->nHeight << hash;
        if (index->pprev == nullptr) // genesis block, all bets are off after this
            return;
        handleFailedBlock(state);
        if (state->m_blockIndex->nHeight == lastFullBlockScheduled)
            --lastFullBlockScheduled;
    }

    chainTipChildren = state->m_chainChildren;
    state->m_blockIndex = nullptr;
    MarkIndexUnsaved(index);
    if (!addToChain)
        return;

    tipFlags = state->flags;

    CValidationState val;
    if (!FlushStateToDisk(val, FLUSH_STATE_IF_NEEDED))
        fatal(val.GetRejectReason().c_str());

    if (state->flags.enableValidation || index->nHeight % 500 == 0)
        logCritical(Log::BlockValidation).nospace() << "new best=" << hash << " height=" << index->nHeight
            << " tx=" << blockchain->Tip()->nChainTx
            << " date=" << DateTimeStrFormat("%Y-%m-%d %H:%M:%S", index->GetBlockTime()).c_str()
            << Log::Fixed << Log::precision(1);
#ifdef ENABLE_BENCHMARKS
    if ((index->nHeight % 1000) == 0) {
        logCritical(Log::Bench) << "Times. Header:" << m_headerCheckTime
                                << "Structure:" << m_basicValidityChecks
                                << "Context:" << m_contextCheckTime
                                << "UTXO:" << m_utxoTime
                                << "validation:" << m_validationTime
                                << "loading:" << m_loadingTime
                                << "mempool:" << m_mempoolTime
                                << "wallet:" << m_walletTime;
    }
    int64_t start = GetTimeMicros();
#endif
    uiInterface.NotifyBlockTip(farBehind, index);
    {
        LOCK(cs_main);
        ValidationNotifier().UpdatedTransaction(hashPrevBestCoinBase);
    }
    hashPrevBestCoinBase = state->m_block.transactions().at(0).createHash();
#ifdef ENABLE_BENCHMARKS
    m_mempoolTime.fetch_add(GetTimeMicros() - start);
#endif
    if (state->m_onResultFlags & Validation::ForwardGoodToPeers) {
        int totalBlocks = Blocks::DB::instance()->headerChain().Height();
        LOCK(cs_vNodes);
        for (CNode* pnode : vNodes) {
            if (blockchain->Height() > totalBlocks - 10)
                pnode->PushBlockHash(hash);
        }
    }
}

void ValidationEnginePrivate::handleFailedBlock(const std::shared_ptr<BlockValidationState> &state)
{
    assert(strand.running_in_this_thread());
    assert(state->m_blockIndex);
    assert(state->m_blockIndex != blockchain->Tip());
    state->recursivelyMark(BlockValidationState::BlockInvalid);
    if (!state->isCorruptionPossible && state->m_blockIndex && state->m_checkMerkleRoot) {
        auto index = state->m_blockIndex;
        state->m_blockIndex->nStatus |= BLOCK_FAILED_VALID;
        // Mark all children as failed too
        for (auto tip : Blocks::DB::instance()->headerChainTips()) {
            if (tip->GetAncestor(index->nHeight) == index) {
                while (tip != index) {
                    tip->nStatus |= BLOCK_FAILED_CHILD;
                    tip = tip->pprev;
                }
            }
        }
        // remember this failed block-id
        mempool->utxo()->setFailedBlockId(state->m_block.createHash());

        auto currentHeaderTip = Blocks::DB::instance()->headerChain().Tip();
        const bool changed = Blocks::DB::instance()->appendHeader(index); // Processes that the block actually is invalid.
        auto tip = Blocks::DB::instance()->headerChain().Tip();
        if (changed && currentHeaderTip != tip) {
            logCritical(Log::BlockValidation).nospace() << "new best header=" << *tip->phashBlock << " height=" << tip->nHeight;
            logInfo(Log::BlockValidation) << "Header-reorg detected. Old-tip" << *currentHeaderTip->phashBlock << "@" << currentHeaderTip->nHeight;
            prepareChain();
        }
    }

    if (state->m_originatingNodeId >= 0) {
        LOCK(cs_main);
        if (state->errorCode < 0xFF)
            queueRejectMessage(state->m_originatingNodeId, state->m_block.createHash(),
                    static_cast<std::uint8_t>(state->errorCode), state->error);
        if (state->m_onResultFlags & Validation::PunishBadNode)
            Misbehaving(state->m_originatingNodeId, state->punishment);
    }

    // TODO rememeber to ignore this blockhash in the 'recently failed' list
}

/*
 * The 'main chain' is determined by the Blocks::DB::headersChain()
 * This method does nothing more than update the real chain to remove blocks that are
 * no longer on the headersChain (due to reorgs, mostly).
 */
void ValidationEnginePrivate::prepareChain()
{
    if (blockchain->Height() <= 0)
        return;
    if (Blocks::DB::instance()->headerChain().Contains(blockchain->Tip()))
        return;
    DEBUGBV << "PrepareChain actually has work to do!";

    std::vector<FastBlock> revertedBlocks;

    LOCK(mempool->cs);
    while (!Blocks::DB::instance()->headerChain().Contains(blockchain->Tip())) {
        CBlockIndex *index = blockchain->Tip();
        logInfo(Log::BlockValidation) << "Removing (rollback) chain tip at" << index->nHeight << index->GetBlockHash();
        FastBlock block;
        try {
            block = Blocks::DB::instance()->loadBlock(index->GetBlockPos());
            revertedBlocks.push_back(block);
            block.findTransactions();
        } catch (const std::runtime_error &error) {
            logFatal(Log::BlockValidation) << "ERROR: Can't undo the tip because I can't find it on disk";
            fatal(error.what());
        }
        if (block.size() == 0)
            fatal("BlockValidationPrivate::prepareChainForBlock: got no block, can't continue.");
        if (!disconnectTip(block, index))
            fatal("Failed to disconnect block");

        tip.store(index->pprev);
    }
    mempool->removeForReorg(blockchain->Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);

    if (revertedBlocks.size() > 3)
        return;
    // Add transactions. Only after we have flushed our removal of transactions from the UTXO view.
    // Otherwise the mempool would object because they would be in conflict with themselves.
    Streaming::BufferPool pool;
    for (int index = revertedBlocks.size() - 1; index >= 0; --index) {
        FastBlock block = revertedBlocks.at(index);
        block.findTransactions();
        for (size_t txIndex = 1; txIndex < block.transactions().size(); txIndex++) {
            Tx tx = block.transactions().at(txIndex);
            std::list<CTransaction> deps;
            mempool->remove(tx.createOldTransaction(), deps, true);

            std::shared_ptr<TxValidationState> state(new TxValidationState(me, tx, TxValidationState::FromMempool));
            state->checkTransaction();

            for (CTransaction tx2 : deps) {// dependent transactions
                state.reset(new TxValidationState(me, Tx::fromOldTransaction(tx2, &pool), TxValidationState::FromMempool));
                state->checkTransaction();
            }
            // Let wallets know transactions went from 1-confirmed to
            // 0-confirmed or conflicted:
            ValidationNotifier().SyncTransaction(tx.createOldTransaction());
            ValidationNotifier().SyncTx(tx);
        }
    }
    mempool->AddTransactionsUpdated(1);
    LimitMempoolSize(*mempool, GetArg("-maxmempool", Settings::DefaultMaxMempoolSize) * 1000000, GetArg("-mempoolexpiry", Settings::DefaultMempoolExpiry) * 60 * 60);
}

void ValidationEnginePrivate::fatal(const char *error)
{
    logFatal(Log::Bitcoin) << "***" << error;
    StartShutdown();
    throw std::runtime_error("App stopping, killing task");
}

void ValidationEnginePrivate::blockLanded(ProcessingType type)
{
    std::unique_lock<decltype(lock)> waitLock(lock);
    int beforeCount;
    if (type == CheckingHeader)
        beforeCount = headersInFlight.fetch_sub(1);
    else
        beforeCount = blocksInFlight.fetch_sub(1);

    DEBUGBV << "headers:" << headersInFlight << "blocks:" << blocksInFlight << "orphans" << orphanBlocks.size();
    if (beforeCount <= blocksInFlightLimit()) {
        waitVariable.notify_all();
        if (!shuttingDown)
            strand.post(std::bind(&ValidationEnginePrivate::findMoreJobs, me.lock()));
    }
}

void ValidationEnginePrivate::findMoreJobs()
{
    assert(strand.running_in_this_thread());
    DEBUGBV << "last scheduled:" << lastFullBlockScheduled;
    if (shuttingDown || engineType == Validation::SkipAutoBlockProcessing)
        return;
    if (lastFullBlockScheduled == -1)
        lastFullBlockScheduled = std::max(0, blockchain->Height());
    while (true) {
        CBlockIndex *index = Blocks::DB::instance()->headerChain()[lastFullBlockScheduled + 1];
        DEBUGBV << "  next:" << index;
        if (index) DEBUGBV << "  " << index->GetBlockHash() << "has data:" << (bool) (index->nStatus & BLOCK_HAVE_DATA);
        if (!(index && (index->nStatus & BLOCK_HAVE_DATA)))
            return;
        assert(index->pprev);
        assert(index->nHeight == lastFullBlockScheduled + 1);
        int currentCount = blocksInFlight.load();
        if (currentCount >= blocksInFlightLimit())
            return;
        int newCount = currentCount + 1;
        if (!blocksInFlight.compare_exchange_weak(currentCount, newCount, std::memory_order_relaxed, std::memory_order_relaxed))
            continue;
        // If we have 1008 validated headers on top of the block, turn off loads of validation of the actual block.
        const bool enableValidation = index->nHeight + 1008 > Blocks::DB::instance()->headerChain().Height();
        int onResultFlags = enableValidation ? Validation::ForwardGoodToPeers : 0;
        if ((index->nStatus & BLOCK_HAVE_UNDO) == 0)
            onResultFlags |= Validation::SaveGoodToDisk;
        std::shared_ptr<BlockValidationState> state = std::make_shared<BlockValidationState>(me, FastBlock(), onResultFlags);
        state->m_blockIndex = index;
        state->flags = tipFlags;
        state->m_blockPos = index->GetBlockPos();
        try {
            state->load();
            if (state->m_block.size() <= 90)
                throw std::runtime_error("Expected full block");
        } catch (const std::runtime_error &e) {
            logWarning(Log::BlockValidation) << "Failed to load block" << state->m_blockPos << "got exception:" << e;
            index->nStatus ^= BLOCK_HAVE_DATA; // obviously not...
            return;
        }
        state->flags.enableValidation = enableValidation;
        state->m_validationStatus = BlockValidationState::BlockValidHeader | BlockValidationState::BlockValidTree;
        state->m_checkingHeader = false;
        blocksBeingValidated.insert(std::make_pair(state->m_block.createHash(), state));

        auto iter = blocksBeingValidated.find(state->m_block.previousBlockId());
        if (iter != blocksBeingValidated.end()) {
            iter->second->m_chainChildren.push_back(state);
        } else if (index->pprev->nChainTx != 0) {
            state->m_validationStatus |= BlockValidationState::BlockValidParent;
        }
        DEBUGBV << "scheduling" <<  lastFullBlockScheduled + 1 << "for validation"
                << (state->flags.enableValidation ? "(full)" : "(shallow)") << "Blocks in flight:" << newCount;
        Application::instance()->ioService().post(std::bind(&BlockValidationState::checks2HaveParentHeaders, state));
        ++lastFullBlockScheduled;
    }
}

bool ValidationEnginePrivate::disconnectTip(const FastBlock &tip, CBlockIndex *index, bool *userClean, bool *error)
{
    assert(index);
    assert(index->pprev);
    assert(tip.createHash() == mempool->utxo()->blockId());
    assert(tip.transactions().size() > 0); // make sure we called findTransactions elsewhere
    assert(strand.running_in_this_thread());

    CDiskBlockPos pos = index->GetUndoPos();
    if (pos.IsNull()) {
        logFatal(Log::BlockValidation) << "No undo data available to disconnectBlock";
        if (error) *error = true;
        return false;
    }
    FastUndoBlock blockUndoFast = Blocks::DB::instance()->loadUndoBlock(pos);
    if (blockUndoFast.size() == 0) {
        logFatal(Log::BlockValidation) << "Failed reading undo data";
        if (error) *error = true;
        return false;
    }

    UnspentOutputDatabase *utxo = mempool->utxo();
    while (true) {
        FastUndoBlock::Item item = blockUndoFast.nextItem();
        if (!item.isValid())
            break;
        if (!item.isInsert())
            utxo->insert(item.prevTxId, item.outputIndex, item.blockHeight, item.offsetInBlock);
    }
    blockUndoFast.restartStream();
    bool clean = true;
    while (true) {
        FastUndoBlock::Item item = blockUndoFast.nextItem();
        if (!item.isValid())
            break;
        if (item.isInsert()) {
            if (!utxo->remove(item.prevTxId, item.outputIndex).isValid())
                clean = false;
        }
    }

    // move best block pointer to prevout block
    utxo->blockFinished(index->pprev->nHeight, index->pprev->GetBlockHash());
    blockchain->SetTip(index->pprev);
    if (userClean) {
        *userClean = clean;
        return true;
    }

    return clean;
}


//---------------------------------------------------------

ValidationFlags::ValidationFlags()
    : strictPayToScriptHash(false),
    enforceBIP34(false),
    enableValidation(true),
    scriptVerifyDerSig(false),
    scriptVerifyLockTimeVerify(false),
    scriptVerifySequenceVerify(false),
    nLocktimeVerifySequence(false),
    hf201708Active(false),
    hf201805Active(false),
    hf201811Active(false),
    hf201905Active(false),
    hf201911Active(false)
{
}

uint32_t ValidationFlags::scriptValidationFlags(bool requireStandard) const
{
    uint32_t flags = strictPayToScriptHash ? SCRIPT_VERIFY_P2SH : SCRIPT_VERIFY_NONE;
    if (scriptVerifyDerSig)
        flags |= SCRIPT_VERIFY_DERSIG;
    if (scriptVerifyLockTimeVerify)
        flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    if (scriptVerifySequenceVerify)
        flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
    if (hf201708Active) {
        flags |= SCRIPT_VERIFY_STRICTENC;
        flags |= SCRIPT_ENABLE_SIGHASH_FORKID;
    }
    if (hf201811Active) {
        flags |= SCRIPT_ENABLE_CHECKDATASIG;
        flags |= SCRIPT_VERIFY_SIGPUSHONLY;
        flags |= SCRIPT_VERIFY_CLEANSTACK;
        flags |= SCRIPT_VERIFY_P2SH; // implied requirement by CLEANSTACK (normally present, but not in unit tests)
    }
    if (hf201905Active) {
        if (!requireStandard)
            flags |= SCRIPT_ALLOW_SEGWIT_RECOVERY;
        flags |= SCRIPT_ENABLE_SCHNORR;
    }
    if (hf201911Active) {
        flags |= SCRIPT_VERIFY_MINIMALDATA;
        flags |= SCRIPT_ENABLE_SCHNORR_MULTISIG;
    }
    return flags;
}

void ValidationFlags::updateForBlock(CBlockIndex *index)
{
    if (index->pprev == nullptr) // skip for genesis block
        return;

    // BIP16 didn't become active until Apr 1 2012
    const int64_t BIP16SwitchTime = 1333238400;
    if (!strictPayToScriptHash && index->nTime >= BIP16SwitchTime)
         strictPayToScriptHash = true; // mainnet: activates on block 173805

    const auto &consensus = Params().GetConsensus();
    if (!enforceBIP34 && index->nHeight >= consensus.BIP34Height && consensus.BIP34Height > 0)
        enforceBIP34 = true;

    // Start enforcing the DERSIG (BIP66) rules
    // Originally this was for block.nVersion=3 blocks, when 75% of the network has upgraded
    // now we just hardcode the height
    if (!scriptVerifyDerSig && index->nHeight >= consensus.BIP66Height)
        scriptVerifyDerSig = true;

    // Start enforcing CHECKLOCKTIMEVERIFY (BIP65)
    // Originally this was for block.nVersion=4 blocks, when 75% of the network has upgraded
    // now we just hardcode the height
    if (!scriptVerifyLockTimeVerify && index->nHeight >= consensus.BIP65Height)
        scriptVerifyLockTimeVerify = true;

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY)
    // This was originally using versionbits logic (bip9).
    // now we just hardcode the height
    if (!scriptVerifySequenceVerify && index->nHeight >= consensus.BIP68Height) {
        scriptVerifySequenceVerify = true;
        nLocktimeVerifySequence = true;
    }

    if (!hf201708Active && index->nHeight >= consensus.hf201708Height)
        hf201708Active = true;
    if (!hf201805Active && index->nHeight >= consensus.hf201805Height)
        hf201805Active = true;
    if (!hf201811Active && index->nHeight >= consensus.hf201811Height)
        hf201811Active = true;
    if (!hf201905Active && index->nHeight >= consensus.hf201905Height)
        hf201905Active = true;
    if (hf201905Active && !hf201911Active && index->GetMedianTimePast() >= consensus.hf201911Time)
        hf201911Active = true;
}

/* TODO Expire orphans.
 * Have a bool in the State object.
 * Have a once-an-hour timer which walks over all orphans. Either set the bool, or delete the orphan if its bool is already set.
 * Unset issuedWarningForVersion every so many hours.
 */

//---------------------------------------------------------

BlockValidationState::BlockValidationState(const std::weak_ptr<ValidationEnginePrivate> &parent, const FastBlock &block, uint32_t onResultFlags, int originatingNodeId)
    : m_block(block),
      m_blockIndex(nullptr),
      m_onResultFlags(static_cast<uint8_t>(onResultFlags)),
      m_originatingNodeId(originatingNodeId),
      m_txChunkLeftToStart(-1),
      m_txChunkLeftToFinish(-1),
      m_validationStatus(BlockValidityUnknown),
      m_blockFees(0),
      m_sigOpsCounted(0),
      m_parent(parent)
{
    assert(onResultFlags < 0x100);
}

BlockValidationState::~BlockValidationState()
{
    std::shared_ptr<ValidationEnginePrivate> parent = m_parent.lock();
    if (parent)
        parent->blockLanded(m_checkingHeader ? ValidationEnginePrivate::CheckingHeader : ValidationEnginePrivate::CheckingBlock);
    if (m_originatingNodeId != -1 && m_block.isFullBlock()) {
        LOCK(cs_main);
        MarkBlockAsReceived(m_block.createHash());
    }
    if (m_ownsIndex)
        delete m_blockIndex;
    if (m_block.size() >= 80)
        DEBUGBV << "Finished" << m_block.createHash();

    for (auto undoItem : m_undoItems) {
        delete undoItem;
    }
    m_undoItems.clear();
}

void BlockValidationState::load()
{
#ifdef ENABLE_BENCHMARKS
    int64_t start = GetTimeMicros();
#endif
    m_block = Blocks::DB::instance()->loadBlock(m_blockPos);
#ifdef ENABLE_BENCHMARKS
    int64_t end = GetTimeMicros();
    auto parent = m_parent.lock();
    if (parent)
        parent->m_loadingTime.fetch_add(end - start);
#endif
    DEBUGBV << "succeeded;" << m_block.createHash() << '/' << m_block.size();
}

void BlockValidationState::blockFailed(int punishment, const std::string &error, Validation::RejectCodes code, bool corruptionPossible)
{
    assert(punishment < 0x100);
    assert(punishment >= 0);
    this->punishment = static_cast<uint8_t>(punishment);
    this->error = error;
    errorCode = code;
    isCorruptionPossible = corruptionPossible;
    m_validationStatus.fetch_or(BlockInvalid);
    auto validationSettings = m_settings.lock();
    if (validationSettings)
        validationSettings->error = error;
}

void BlockValidationState::signalChildren() const
{
    for (auto child_weak : m_chainChildren) {
        std::shared_ptr<BlockValidationState> child = child_weak.lock();
        if (child.get()) {
            assert(child->m_blockIndex->nHeight == m_blockIndex->nHeight + 1);
            int status = child->m_validationStatus.load();
            while (true) {
                int newStatus = status | BlockValidationState::BlockValidParent;
                assert(newStatus != status);
                if  (child->m_validationStatus.compare_exchange_weak(status, newStatus, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    if (status & BlockValidationState::BlockValidChainHeaders)
                        Application::instance()->ioService().post(std::bind(&BlockValidationState::updateUtxoAndStartValidation, child));
                    break;
                }
            }
        }
    }
}

void BlockValidationState::recursivelyMark(BlockValidationStatus value, RecursiveOption option)
{
    if (option == AddFlag)
        m_validationStatus.fetch_or(value);
    else
        m_validationStatus.fetch_and(0xFF^value);
    for (auto child : m_chainChildren) {
        std::shared_ptr<BlockValidationState> state = child.lock();
        if (state)
            state->recursivelyMark(value, option);
    }
}

void BlockValidationState::finishUp()
{
    std::shared_ptr<ValidationEnginePrivate> parent = m_parent.lock();
    if (parent)
        parent->strand.post(std::bind(&ValidationEnginePrivate::processNewBlock, parent, shared_from_this()));
}

void BlockValidationState::checks1NoContext()
{
    try {
        if (m_block.size() == 0)
            load();
    } catch (const std::exception &e) {
        logInfo(Log::BlockValidation) << "BlockValidationState: Failed to load block, ignoring. Error:" << e.what()
                  << "File idx:" << m_blockPos.nFile << "pos:" << m_blockPos.nPos;
        auto validationSettings = m_settings.lock();
        if (validationSettings) {
            validationSettings->error = std::string("Failed to load block. Error: ") + e.what();
            validationSettings->markFinished();
        }
        return;
    }
#ifdef ENABLE_BENCHMARKS
    int64_t start2, end2, end, start; start2 = end2 = start = end = GetTimeMicros();
#endif

    DEBUGBV << "Starting" << m_block.createHash() << "CheckPOW:" << m_checkPow << "CheckMerkleRoot:" << m_checkMerkleRoot << "ValidityOnly:" << m_checkValidityOnly;

    try { // These are checks that are independent of context.
        // Check proof of work matches claimed amount
        if (m_checkPow && !CheckProofOfWork(m_block.createHash(), m_block.bits(), Params().GetConsensus()))
            throw Exception("high-hash", 50);

        // Check timestamp
        if (m_block.timestamp() > GetAdjustedTime() + 2 * 60 * 60)
            throw Exception("time-too-new");

#ifdef ENABLE_BENCHMARKS
        start2 = end = GetTimeMicros();
#endif
        // if this is a full block, test the transactions too.
        if (m_block.isFullBlock() && m_checkTransactionValidity) {
            m_block.findTransactions(); // find out if the block and its transactions are well formed and parsable.

            CBlock block = m_block.createOldBlock();
            if (m_checkMerkleRoot) { // Check the merkle root.
                bool mutated;
                uint256 hashMerkleRoot2 = BlockMerkleRoot(block, &mutated);
                if (block.hashMerkleRoot != hashMerkleRoot2)
                    throw Exception("bad-txnmrklroot", Validation::InvalidNotFatal);

                // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
                // of transactions in a block without affecting the merkle root of a block,
                // while still invalidating it.
                if (mutated)
                    throw Exception("bad-txns-duplicate", Validation::InvalidNotFatal);
            }

            // Size limits
            if (block.vtx.empty()) {
                logCritical(Log::BlockValidation) << "Block has no transactions, not even a coinbase. Rejecting";
                throw Exception("bad-blk-length");
            }

            const std::int32_t blockSizeAcceptLimit = Policy::blockSizeAcceptLimit();
            const std::int32_t blockSize = m_block.size();
            if (blockSize > blockSizeAcceptLimit) {
                const float punishment = (blockSize - blockSizeAcceptLimit) / float(blockSizeAcceptLimit);
                logCritical(Log::BlockValidation) << "Block too large" << blockSize << ">" << blockSizeAcceptLimit;
                throw Exception("bad-blk-length", Validation::RejectExceedsLimit, int(10 * punishment + 0.5f));
            }

            // All potential-corruption validation must be done before we do any
            // transaction validation, as otherwise we may mark the header as invalid
            // because we receive the wrong transactions for it.

            assert(!block.vtx.empty());
            // First transaction must be coinbase, the rest must not be
            if (!block.vtx[0].IsCoinBase())
                throw Exception("bad-cb-missing");
            for (unsigned int i = 1; i < block.vtx.size(); i++) {
                if (block.vtx[i].IsCoinBase())
                    throw Exception("bad-cb-multiple");
            }

            // Check transactions
            // TODO chunk this over all CPUs
            for (const CTransaction &tx : block.vtx) {
                Validation::checkTransaction(tx);
            }
        }

        m_validationStatus.fetch_or(BlockValidHeader);
#ifdef ENABLE_BENCHMARKS
        end2 = GetTimeMicros();
#endif
    } catch (const Exception &ex) {
        blockFailed(ex.punishment(), ex.what(), ex.rejectCode(), ex.corruptionPossible());
    } catch (const std::runtime_error &ex) {
        assert(false);
        blockFailed(100, ex.what(), Validation::RejectInternal);
    }

    std::shared_ptr<ValidationEnginePrivate> parent = m_parent.lock();
    if (parent) {
#ifdef ENABLE_BENCHMARKS
        parent->m_headerCheckTime.fetch_add(end - start);
        parent->m_basicValidityChecks.fetch_add(end2 - start2);
#endif
        parent->strand.dispatch(std::bind(&ValidationEnginePrivate::blockHeaderValidated, parent, shared_from_this()));
    }
}

void BlockValidationState::checks2HaveParentHeaders()
{
    assert(m_blockIndex);
    assert(m_blockIndex->nHeight >= 0);
    assert(m_block.isFullBlock());
    DEBUGBV << m_blockIndex->nHeight << m_block.createHash();

#ifdef ENABLE_BENCHMARKS
    int64_t start = GetTimeMicros();
#endif
    try {
        m_block.findTransactions();
        CBlock block = m_block.createOldBlock();
        if (m_blockIndex->pprev) { // not genesis
            const auto consensusParams = Params().GetConsensus();
            // Check proof of work
            if (block.nBits != GetNextWorkRequired(m_blockIndex->pprev, &block, consensusParams))
                throw Exception("bad-diffbits");

            // Check timestamp against prev
            if (block.GetBlockTime() <= m_blockIndex->pprev->GetMedianTimePast())
                throw Exception("time-too-old");
            if (block.nVersion < 4 && flags.scriptVerifyLockTimeVerify) // reject incorrect block version.
                throw Exception("bad-version", Validation::RejectObsolete);
        }

        // Check that all transactions are finalized
        const int64_t nLockTimeCutoff = flags.scriptVerifySequenceVerify ? m_blockIndex->pprev->GetMedianTimePast() : block.GetBlockTime();
        for (const CTransaction& tx : block.vtx) {
            if (!IsFinalTx(tx, m_blockIndex->nHeight, nLockTimeCutoff))
                throw Exception("bad-txns-nonfinal");
        }

        // Enforce rule that the coinbase starts with serialized block height
        if (flags.enforceBIP34) {
            CScript expect = CScript() << m_blockIndex->nHeight;
            if (block.vtx[0].vin[0].scriptSig.size() < expect.size()
                    || !std::equal(expect.begin(), expect.end(), block.vtx[0].vin[0].scriptSig.begin()))
                throw Exception("bad-cb-height");
        }

        // Sigops.
        // Notice that we continue counting in validateTransactionInputs and do one last check in processNewBlock()
        // TODO chunk this over all CPUs
        uint32_t sigOpsCounted = 0;
        for (const CTransaction &tx : block.vtx)
            sigOpsCounted += Validation::countSigOps(tx);
        const uint32_t maxSigOps = Policy::blockSigOpAcceptLimit(m_block.size());
        if (sigOpsCounted > maxSigOps)
            throw Exception("bad-blk-sigops");
        assert(m_sigOpsCounted == 0);
        m_sigOpsCounted = sigOpsCounted;

        if (flags.hf201811Active) {
            for (auto tx : m_block.transactions()) {
                // Impose a minimum transaction size of 100 bytes after the Nov, 15 2018 HF
                // this is stated to be done to avoid a leaf node weakness in bitcoin's merkle tree design
                if (tx.size() < 100)
                    throw Exception("bad-txns-undersize");
            }
        }
    } catch (const Exception &e) {
        blockFailed(e.punishment(), e.what(), e.rejectCode(), e.corruptionPossible());
        finishUp();
        return;
    } catch (std::runtime_error &e) {
        assert(false);
        blockFailed(100, e.what(), Validation::RejectInternal);
        finishUp();
        return;
    }

    flags.updateForBlock(m_blockIndex);
#ifdef ENABLE_BENCHMARKS
    int64_t end = GetTimeMicros();
#endif

    int status = m_validationStatus.load();
    auto parent = m_parent.lock();
    while (parent) {
#ifdef ENABLE_BENCHMARKS
        parent->m_contextCheckTime.fetch_add(end - start);
#endif
        int newStatus = status | BlockValidChainHeaders;
        if  (m_validationStatus.compare_exchange_weak(status, newStatus, std::memory_order_relaxed, std::memory_order_relaxed)) {
            if ((status & BlockValidParent) || (status & BlockInvalid)) { // we just added the last bit.
                Application::instance()->ioService().post(std::bind(&BlockValidationState::updateUtxoAndStartValidation, shared_from_this()));
            } else {
                assert(!m_checkValidityOnly); // why did we get here if the
                DEBUGBV << "  saving block for later, no parent yet" << m_block.createHash()
                        << '@' << m_blockIndex->nHeight << "parent:" << m_blockIndex->pprev->GetBlockHash();
            }
            return;
        }
    }
}

void BlockValidationState::updateUtxoAndStartValidation()
{
    DEBUGBV << m_block.createHash();
    assert(m_txChunkLeftToStart.load() < 0); // this method should get called only once
    auto parent = m_parent.lock();
    if (!parent)
        return;

    assert(parent->blockchain->Tip() == nullptr || parent->blockchain->Tip()->nHeight <= m_blockIndex->nHeight);

    if (m_blockIndex->pprev == nullptr) { // genesis
        finishUp();
        return;
    }

    try {
        assert (m_block.transactions().size() > 0);
        // inserting all outputs that are created in this block first.
        // we do this in a single thread since inserting massively parallel will just cause a huge overhead
        // and we'd end up being no faster while competing for the scarce resources that are the UTXO DB
        UnspentOutputDatabase::BlockData data;
        data.blockHeight = m_blockIndex->nHeight;
        data.outputs.reserve(m_block.transactions().size());
        Tx::Iterator iter = Tx::Iterator(m_block);
        int outputCount = 0, txIndex = 0;
        uint256 prevTxHash;
        while (true) {
            const auto type = iter.next();
            if (type == Tx::End) {
                Tx tx = iter.prevTx();
                const int offsetInBlock = tx.offsetInBlock(m_block);
                assert(tx.isValid());
                const uint256 txHash = tx.createHash();
                if (flags.hf201811Active && txIndex > 1 && txHash.Compare(prevTxHash) <= 0)
                    throw Exception("tx-ordering-not-CTOR");
                data.outputs.push_back(UnspentOutputDatabase::BlockData::TxOutputs(txHash, offsetInBlock, 0, outputCount - 1));
                outputCount = 0;
                if (flags.hf201811Active)
                    prevTxHash = txHash;
                ++txIndex;
                if (iter.next() == Tx::End) // double end: last tx in block
                    break;
            }
            else if (iter.tag() == Tx::OutputValue) { // next output!
                // if (iter.longData() == 0) logDebug(Log::BlockValidation) << "Output with zero value";
                outputCount++;
            }
        }

        int chunks, itemsPerChunk;
        if (m_checkValidityOnly) { // no UTXO interaction allowed.
            chunks = 1;
            itemsPerChunk = m_block.transactions().size();

            for (auto tx : data.outputs) {
                assert(tx.firstOutput == 0);
                std::deque<std::pair<int, int> > outputs;
                for (int i = 0; i <= tx.lastOutput; ++i) {
                    outputs.push_back(std::make_pair(i, tx.offsetInBlock));
                }
                DEBUGBV << "available in this block: " << tx.txid << tx.lastOutput;
                m_txMap.insert(std::make_pair(tx.txid, outputs));
            }
        }
        else {
            calculateTxCheckChunks(chunks, itemsPerChunk);
#ifdef ENABLE_BENCHMARKS
            int64_t start = GetTimeMicros();
#endif
            parent->mempool->utxo()->insertAll(data);
#ifdef ENABLE_BENCHMARKS
            int64_t end = GetTimeMicros();
            parent->m_utxoTime.fetch_add(end - start);
#endif
        }
        m_txChunkLeftToFinish.store(chunks);
        m_txChunkLeftToStart.store(chunks);
        m_undoItems.resize(static_cast<size_t>(chunks));

        for (int i = 0; i < chunks; ++i) {
            Application::instance()->ioService().post(std::bind(&BlockValidationState::checkSignaturesChunk,
                                                                shared_from_this()));
        }
    } catch(const UTXOInternalError &ex) {
        parent->fatal(ex.what());
    } catch(const Exception &ex) {
        blockFailed(ex.punishment(), ex.what(), ex.rejectCode(), ex.corruptionPossible());
        finishUp();
    } catch(const std::exception &ex) {
        assert(false);
        blockFailed(100, ex.what(), Validation::RejectInternal);
        finishUp();
    }
}

void BlockValidationState::checkSignaturesChunk()
{
#ifdef ENABLE_BENCHMARKS
    int64_t start = GetTimeMicros();
    int64_t utxoStart, utxoDuration = 0;
#endif
    auto parent = m_parent.lock();
    if (!parent)
        return;
    assert(parent->mempool);
    UnspentOutputDatabase *utxo = parent->mempool->utxo();
    assert(utxo);
    const int totalTxCount = static_cast<int>(m_block.transactions().size());

    int chunkToStart = m_txChunkLeftToStart.fetch_sub(1) - 1;
    assert(chunkToStart >= 0);
    DEBUGBV << chunkToStart << m_block.createHash();

    int chunks, itemsPerChunk;
    if (m_checkValidityOnly) {
        chunks = 1; itemsPerChunk = totalTxCount;
    } else {
        calculateTxCheckChunks(chunks, itemsPerChunk);
    }
    bool blockValid = (m_validationStatus.load() & BlockInvalid) == 0;
    int txIndex = itemsPerChunk * chunkToStart;
    const int txMax = std::min(txIndex + itemsPerChunk, totalTxCount);
    uint32_t chunkSigops = 0;
    CAmount chunkFees = 0;
    std::unique_ptr<std::deque<FastUndoBlock::Item> >undoItems(new std::deque<FastUndoBlock::Item>());

    try {
        for (;blockValid && txIndex < txMax; ++txIndex) {
            CAmount fees = 0;
            uint32_t sigops = 0;
            Tx tx = m_block.transactions().at(static_cast<size_t>(txIndex));
            const uint256 hash = tx.createHash();

            std::vector<ValidationPrivate::UnspentOutput> unspents; // list of prev outputs
            auto txIter = Tx::Iterator(tx);
            auto inputs = Tx::findInputs(txIter);
            if (txIndex == 0)
                inputs.clear(); // skip inputs check for coinbase
            std::vector<int> prevheights; // the height of each input
            for (auto input : inputs) { // find inputs
                ValidationPrivate::UnspentOutput prevOut;
#ifdef ENABLE_BENCHMARKS
                utxoStart = GetTimeMicros();
#endif
                UnspentOutput unspentOutput = utxo->find(input.txid, input.index);
#ifdef ENABLE_BENCHMARKS
                utxoDuration += GetTimeMicros() - utxoStart;
#endif
                bool validUtxo = unspentOutput.isValid();
                bool validInterBlockSpent = validUtxo; // ONLY used when m_validityOnly is true!
                if (!validUtxo && m_checkValidityOnly) {
                    // in the checkValidity case we don't touch the UTXO and as such some inter-block spending may
                    // give a false-positive. Check that using the m_txMap structure
                    auto ti = m_txMap.find(input.txid);
                    if (ti != m_txMap.end()) {
                        for (auto iter = ti->second.begin(); iter != ti->second.end(); ++iter) {
                            if (iter->first == input.index) {
                                // found index.
                                prevheights.push_back(m_blockIndex->nHeight);
                                if (flags.enableValidation) {
                                    int output = input.index;
                                    assert(output >= 0);
                                    Tx::Iterator prevTxIter(m_block, iter->second);
                                    while (output-- >= 0) {
                                        prevTxIter.next(Tx::OutputValue);
                                    }
                                    prevOut.amount = static_cast<int64_t>(prevTxIter.longData());
                                    prevTxIter.next();
                                    assert(prevTxIter.tag() == Tx::OutputScript);
                                    prevOut.outputScript = prevTxIter.byteData();
                                    prevOut.blockheight = m_blockIndex->nHeight;
                                    unspents.push_back(prevOut);
                                }
                                validInterBlockSpent = true;
                                ti->second.erase(iter);
                                break;
                            }
                        }
                        if (!validInterBlockSpent)
                            DEBUGBV << "Found txid in m_txMap, but not the wanted output...";
                    }
                }
                if (!validUtxo && !validInterBlockSpent) {
                    logCritical(Log::BlockValidation) << "Rejecting block" << m_block.createHash() << "due to missing inputs";
                    logInfo(Log::BlockValidation) << " + txid:" << tx.createHash() << "needs input:" << input.txid << input.index;
                    throw Exception("missing-inputs", 0);
                }
                if (m_checkValidityOnly && validUtxo) {
                    // we just checked the UTXO, but when m_checkValidityOnly is true
                    // the output is not removed from the UTXO, and as such we need a bit of extra code
                    // to detect double-spends.
                    auto ti = m_spentMap.find(input.txid);
                    if (ti != m_spentMap.end()) {
                        for (int index : ti->second) {
                            if (index == input.index) // already spent the UTXO!
                                throw Exception("missing-inputs", 0);
                        }
                        ti->second.push_back(input.index);
                    } else {
                        std::deque<int> spentIndex = { input.index };
                        m_spentMap.insert(std::make_pair(input.txid, spentIndex));
                    }
                }
                if (validUtxo) { // fill prevHeight and unspents from the UTXO
                    prevheights.push_back(unspentOutput.blockHeight());
                    if (flags.enableValidation) {
                        UnspentOutputData data(unspentOutput);
                        prevOut.amount = data.outputValue();
                        prevOut.outputScript = data.outputScript();
                        prevOut.blockheight = data.blockHeight();
                        unspents.push_back(prevOut);
                    }
                }

                if (!m_checkValidityOnly) {
#ifdef ENABLE_BENCHMARKS
                    utxoStart = GetTimeMicros();
#endif
                    SpentOutput removed = utxo->remove(input.txid, input.index, unspentOutput.rmHint());
#ifdef ENABLE_BENCHMARKS
                    utxoDuration += GetTimeMicros() - utxoStart;
#endif
                    if (!removed.isValid()) {
                        logCritical(Log::BlockValidation) << "Rejecting block" << m_block.createHash() << "due to deleted input";
                        logInfo(Log::BlockValidation) << " + txid:" << tx.createHash() << "needs input:" << input.txid << input.index;
                        throw Exception("missing-inputs", 0);
                    }
                    assert(input.index >= 0);
                    assert(removed.blockHeight > 0);
                    assert(removed.offsetInBlock > 80);
                    undoItems->push_back(FastUndoBlock::Item(input.txid, input.index,
                                                               removed.blockHeight, removed.offsetInBlock));
                }
            }

            if (flags.enableValidation && txIndex > 0) {
                CTransaction old = tx.createOldTransaction();
                // Check that transaction is BIP68 final
                int nLockTimeFlags = 0;
                if (flags.nLocktimeVerifySequence)
                    nLockTimeFlags |= LOCKTIME_VERIFY_SEQUENCE;
                if (!SequenceLocks(old, nLockTimeFlags, &prevheights, *m_blockIndex))
                    throw Exception("bad-txns-nonfinal");

                bool spendsCoinBase;
                ValidationPrivate::validateTransactionInputs(old, unspents, m_blockIndex->nHeight, flags, fees,
                                                             sigops, spendsCoinBase, /* requireStandard */ false);
                chunkSigops += sigops;
                chunkFees += fees;
            }

            if (!m_checkValidityOnly) {
                DEBUGBV << "add outputs from TX " << txIndex;
                // Find the outputs added to the unspentOutputDB
                int outputCount = 0;
                auto content = txIter.tag();
                while (content != Tx::End) {
                    if (content == Tx::OutputValue) {
                        // if (txIter.longData() == 0) logDebug(Log::BlockValidation) << "Output with zero value";
                        undoItems->push_back(FastUndoBlock::Item(hash, outputCount));
                        outputCount++;
                    }
                    content = txIter.next(Tx::OutputValue + Tx::End);
                }
            }
        }
    } catch(const UTXOInternalError &ex) {
        parent->fatal(ex.what());
    } catch (const Exception &e) {
        DEBUGBV << "Failed validation due to" << e.what();
        blockFailed(e.punishment(), e.what(), e.rejectCode(), e.corruptionPossible());
        blockValid = false;
    } catch (const std::runtime_error &e) {
        DEBUGBV << "Failed validation due to" << e.what();
        blockFailed(100, e.what(), Validation::RejectMalformed);
        blockValid = false;
    }
    m_blockFees.fetch_add(chunkFees);
    m_sigOpsCounted.fetch_add(chunkSigops);
    m_undoItems[static_cast<size_t>(chunkToStart)] = undoItems.release();

#ifdef ENABLE_BENCHMARKS
    int64_t end = GetTimeMicros();
    if (blockValid) {
        parent_->m_validationTime.fetch_add(end - start - utxoDuration);
        parent_->m_utxoTime.fetch_add(utxoDuration);
    }
    logDebug(Log::BlockValidation) << "batch:" << chunkToStart << '/' << chunks << (end - start)/1000. << "ms" << "success so far:" << blockValid;
#endif

    const int chunksLeft = m_txChunkLeftToFinish.fetch_sub(1) - 1;
    if (chunksLeft <= 0) // I'm the last one to finish
        finishUp();
}
