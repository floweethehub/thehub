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
#include "BlockValidation_p.h"
#include "TxValidation_p.h"
#include "ValidationException.h"
#include <Application.h>
#include <main.h>
#include <net.h>
#include <Logger.h>
#include <txorphancache.h>
#include <utxo/UnspentOutputDatabase.h>
#include <server/BlocksDB.h>

// #define DEBUG_BLOCK_VALIDATION
#ifdef DEBUG_BLOCK_VALIDATION
# define DEBUGBV logCritical(Log::BlockValidation)
#else
# define DEBUGBV BTC_NO_DEBUG_MACRO()
#endif

// #define DEBUG_TRANSACTION_VALIDATION
#ifdef DEBUG_TRANSACTION_VALIDATION
# define DEBUGTX logCritical(Log::TxValidation)
#else
# define DEBUGTX BTC_NO_DEBUG_MACRO()
#endif

using Validation::Exception;

Validation::Engine::Engine(EngineType type)
    : d(new ValidationEnginePrivate(type))
{
    d->me = priv();
}

Validation::Engine::~Engine()
{
    // shutdown is a little odd. We don't delete our 'BlockValidationPrivate' instance!
    // instead we tell it to shutdown and we reset the shared pointer.
    // any jobs that are still running will stop eventually and they will then also
    // stop referencing our 'BlockValidationPrivate' instance and when that count hits zero,
    // then our BlockValidationPrivate will be deleted.
    shutdown();
}

Validation::Settings Validation::Engine::addBlock(const FastBlock &block, std::uint32_t onResultFlags, CNode *pFrom)
{
    assert(onResultFlags < 8);
    if (!d.get() || d->shuttingDown)
        return Validation::Settings();
#ifdef DEBUG_BLOCK_VALIDATION
    if (block.size() < 80)
        DEBUGBV << "Block too small" << block.size() << '/' << block.previousBlockId() << "Headers in flight:"
            << d->headersInFlight.load() << (block.isFullBlock() ? "":"[header]");
    else
        DEBUGBV << block.createHash() << '/' << block.previousBlockId() << "Headers in flight:"
                << d->headersInFlight.load() << (block.isFullBlock() ? "":"[header]");
#endif
    d->headersInFlight.fetch_add(1);

    std::shared_ptr<BlockValidationState> state(new BlockValidationState(priv(), block, onResultFlags, pFrom ? pFrom->id : -1));
    Validation::Settings settings;
    settings.d->state = state;
    state->m_settings = settings.d;
    return settings;
}

Validation::Settings Validation::Engine::addBlock(const CDiskBlockPos &pos, std::uint32_t onResultFlags)
{
    assert(onResultFlags < 8);
    if (!d.get() || d->shuttingDown)
        return Validation::Settings();
    d->headersInFlight.fetch_add(1);
    DEBUGBV << d->headersInFlight.load();

    FastBlock dummy;
    std::shared_ptr<BlockValidationState> state(new BlockValidationState(priv(), dummy, onResultFlags));
    state->m_blockPos = pos;
    Validation::Settings settings;
    settings.d->state = state;
    state->m_settings = settings.d;
    return settings;
}

std::future<std::string> Validation::Engine::addTransaction(const Tx &tx, uint32_t onResultFlags, CNode *pFrom)
{
    assert(onResultFlags < 0x40);
    assert((onResultFlags & SaveGoodToDisk) == 0);
    if (!d.get() || d->shuttingDown) {
        std::promise<std::string> promise;
        promise.set_value(std::string());
        return promise.get_future();
    }
    const uint256 hash = tx.createHash();
    std::shared_ptr<TxValidationState> state(new TxValidationState(priv(), tx, onResultFlags));
    bool start = true;
    {
        std::lock_guard<std::mutex> rejects(d->recentRejectsLock);
        if (d->recentTxRejects.contains(hash))
            start = false;
    }
    if (start && CTxOrphanCache::contains(hash))
        start = false;
    DEBUGTX << tx.createHash() << tx.size() << "will start:" << start;

    if (pFrom)
        state->m_originatingNodeId = pFrom->id;
    if (start)
        Application::instance()->ioService().post(std::bind(&TxValidationState::checkTransaction, state));
    return state->m_promise.get_future();
}

void Validation::Engine::waitForSpace()
{
    std::shared_ptr<ValidationEnginePrivate> dd(d); // Make sure this method is re-entrant
    if (!dd.get() || dd->shuttingDown)
        return;
    std::unique_lock<decltype(dd->lock)> lock(dd->lock);
    while (!dd->shuttingDown && dd->headersInFlight >= dd->blocksInFlightLimit())
        dd->waitVariable.wait(lock);
}

void Validation::Engine::waitValidationFinished()
{
    std::shared_ptr<ValidationEnginePrivate> dd(d); // Make sure this method is re-entrant
    if (!dd.get())
        return;
    std::unique_lock<decltype(dd->lock)> lock(dd->lock);
    while (!dd->shuttingDown && (dd->headersInFlight > 0 || dd->blocksInFlight > 0))
        dd->waitVariable.wait(lock);
}

std::weak_ptr<ValidationEnginePrivate> Validation::Engine::priv()
{
    return d;
}

void Validation::Engine::setBlockchain(CChain *chain)
{
    assert(chain);
    if (!d.get() || d->shuttingDown)
        return;
    d->blockchain = chain;
    d->tip = chain->Tip();

    if (chain->Height() > 1)
        d->tipFlags.updateForBlock(chain->Tip(), chain->Tip()->GetBlockHash());
}

bool Validation::Engine::isRecentlyRejectedTransaction(const uint256 &txHash) const
{
    if (!d.get() || d->shuttingDown)
        return false;
    std::lock_guard<std::mutex> lock(d->recentRejectsLock);
    return d->recentTxRejects.contains(txHash);
}

CChain *Validation::Engine::blockchain() const
{
    if (!d.get() || d->shuttingDown)
        return nullptr;
    return d->blockchain;
}

void Validation::Engine::setMempool(CTxMemPool *mempool)
{
    if (!d.get() || d->shuttingDown)
        return;
    d->mempool = mempool;
}

CTxMemPool *Validation::Engine::mempool() const
{
    if (!d.get() || d->shuttingDown)
        return nullptr;
    return d->mempool;
}

void Validation::Engine::invalidateBlock(CBlockIndex *index)
{
    assert(index);
    if (!d.get() || d->shuttingDown)
        return;
    // Mark the block itself as invalid.
    index->nStatus |= BLOCK_FAILED_VALID;
    MarkIndexUnsaved(index);
    Blocks::DB::instance()->appendHeader(index);
    WaitUntilFinishedHelper helper(std::bind(&ValidationEnginePrivate::prepareChain_priv, d), &d->strand);
    helper.run();
}

void ValidationEnginePrivate::prepareChain_priv()
{
    prepareChain();
    lastFullBlockScheduled = -1;
    findMoreJobs();
}

bool Validation::Engine::disconnectTip(const FastBlock &tip, CBlockIndex *index, bool *userClean)
{
    assert(index);
    assert(tip.isFullBlock());
    assert(d->mempool);
    assert(d->mempool->utxo());
    assert(tip.createHash() == d->mempool->utxo()->blockId());

    if (!d.get() || d->shuttingDown)
        return true;

    bool clean = true;
    bool error = false; // essentially our return-value, since our helper doesn't remember that.
    WaitUntilFinishedHelper helper(std::bind(&ValidationEnginePrivate::disconnectTip, d, tip, index, &clean, &error), &d->strand);
    helper.run();
    if (userClean) {
        *userClean = clean;
        return !error;
    }
    return clean && !error;
}

void Validation::Engine::shutdown()
{
    if (!d.get())
        return;
    d->shuttingDown = true;
    WaitUntilFinishedHelper helper(std::bind(&ValidationEnginePrivate::cleanup, d), &d->strand);
    d.reset();
    helper.run();
}

void Validation::Engine::start()
{
    d->strand.post(std::bind(&ValidationEnginePrivate::findMoreJobs, d));
}
