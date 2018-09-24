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

#ifndef VALIDATIONENGINE_H
#define VALIDATIONENGINE_H

#include "ValidationSettings.h"

class CNode;
struct CDiskBlockPos;
class ValidationEnginePrivate;
class CChain;
class FastBlock;
class CTxMemPool;
class CTransaction;
class Tx;

namespace Blocks {
class DB;
}

#include <memory>
#include <future>
#include <uint256.h>

namespace Validation {
/**
 * @brief The ResultHandlingFlags enum explains what to do after a block finished validation.
 */
enum ResultHandlingFlags {
    SaveGoodToDisk = 1,    ///< Successful blocks get saved to disk.
    ForwardGoodToPeers = 2,///< A successful block will get forwarded to peers.
    PunishBadNode = 4,     ///< Ban a bad node that gave us this block.
    RateLimitFreeTx = 8,
    RejectAbsurdFeeTx = 0x10
};

/// throws exception if transaction is malformed.
void checkTransaction(const CTransaction &tx);

enum EngineType {
    FullEngine,

    /**
     * In the case of VerifyDB we need an engine that allows going backwards in time.
     * Removing items *against* the blocks-db. For this unique usecase we need to skip
     * automatically loading and processing blocks that have previously been added to the
     * header-chain.
     */
    SkipAutoBlockProcessing
};

/**
 * @brief The Engine class does all block & transaction validation and processing.
 * This class is an abstraction to simplify the validation process of foreign data being shared
 * with this node and validating its correctness before accepting it into the node.
 *
 * This class is multi-threaded for parallel validation, both of block headers and all other parts where possible.
 * The actual multi-threading is done in a way that is lock-free.
 *
 * Should there be a large backlog of blocks the headers will be validated first at a much higher pace than the actual
 * block content and based on validated blocks we choose which full blocks to start validation on.
 * An additional feature that this allows us is that blocks that have half a difficulty-adjustment period of
 * validated headers (1008) build already on top of them, they skip validation of script signatures to allow
 * catching up of a node to be as fast as possible, without sacrificing security.
 *
 * @see Application::validation()
 */
class Engine
{
public:
    Engine(EngineType type = FullEngine);
    ~Engine();

    /**
     * @brief add a block to the validation queue.
     * This takes a block schedules the validation on all the threads available. This method returns immediately.
     * @param block the block you want to validate.
     * @param onResultFlags indicates what should happen after validation completes.
     * @param pFrom the originating node that send us this. Needed if its a bad block and we want to punish it.
     * @see waitForSpace
     */
    Settings addBlock(const FastBlock &block, std::uint32_t onResultFlags, CNode *pFrom = nullptr);

    Settings addBlock(const CDiskBlockPos &pos, std::uint32_t onResultFlags = 0);

    /*
     * Schedule the transaction validation for correctness and addition to the mempool.
     */
    std::future<std::string> addTransaction(const Tx &tx, std::uint32_t onResultFlags = 0, CNode *pFrom = nullptr);

    /**
     * @brief waitForSpace is a potentially blocking method that waits until the job count drops to an acceptable level.
     * Due to addBlock() starting an async process it returns immediately and as such a user that expects to add
     * a large number of blocks should avoid overloading the system by waiting in between calls to addBlock for space to
     * free up from blocks that finished validation.
     */
    void waitForSpace();

    /**
     * This method blocks until all validation tasks are done.
     */
    void waitValidationFinished();

    /**
     * Set the block chain we should operate on. This is mandatory data.
     */
    void setBlockchain(CChain *chain);

    bool isRecentlyRejectedTransaction(const uint256 &txHash) const;

    /**
     * @brief get the block chain that was set using setBlockchain()
     */
    CChain* blockchain() const;

    void setMempool(CTxMemPool *mempool);
    CTxMemPool *mempool() const;

    void invalidateBlock(CBlockIndex *index);

    /**
     * Undo the effects of this block (with given index) on the UTXO set represented by view.
     * @param tip the block to undo,
     * @param index the blockindex representing the block that we should undo.
     * @param the UTXO view we should undo the changes on.
     * @param clean is an optional bool that is set based on non-fatal issues in disconnecting,
     *  essentially it is set to false if the reverse-apply of the block was non-clean.
     *  Notice that passing in a non-null value adjusts the return value!.
     *
     * @return true if all went well.  If \a clean was null and the application was not clean, then we also return false.
     *
     * Note that in any case, coins may be modified.
     */
    bool disconnectTip(const FastBlock &tip, CBlockIndex *index, bool *clean = nullptr);

    /**
     * Request the validation engine to stop validating.
     * This call blocks until many parts are stopped.
     *
     * Notice that this method is also called from the destructor.
     */
    void shutdown();

    /**
     * A fully initialized validation engine is idling until something
     * is added.
     * If a backlog of blocks to check was left last shutdown, calling start
     * will start processing those.
     */
    void start();

    /// \internal
    std::weak_ptr<ValidationEnginePrivate> priv();

private:
    std::shared_ptr<ValidationEnginePrivate> d;
};
}

#endif
