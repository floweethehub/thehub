/*
 * This file is part of the Flowee project
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

#include "VerifyDB.h"
#include <primitives/FastBlock.h>
#include <Application.h>
#include <coins.h>
#include <UiInterface.h>
#include <main.h>
#include <util.h>
#include <chain.h>
#include <init.h>
#include <txmempool.h>

VerifyDB::VerifyDB()
    : validator(Validation::SkipAutoBlockProcessing)
{
    uiInterface.ShowProgress(_("Verifying blocks..."), 0);
}

VerifyDB::~VerifyDB()
{
    uiInterface.ShowProgress("", 100);
}

bool VerifyDB::verifyDB(CCoinsView *coinsview, int nCheckLevel, int nCheckDepth)
{
    auto global = Application::instance()->validation();
    const CChain *globalChain = global->blockchain();
    assert(globalChain);
    if (!globalChain->Tip() || globalChain->Height() < 5)
        return true;

    CChain blockchain(*globalChain);
    validator.setBlockchain(&blockchain);
    CCoinsViewCache coins(coinsview);
    CTxMemPool pool;
    pool.setCoinsView(&coins);
    validator.setMempool(&pool);

    if (nCheckDepth <= 0)
        nCheckDepth = 1000000000; // suffices until the year 19000
    if (nCheckDepth > blockchain.Height())
        nCheckDepth = blockchain.Height();
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    logCritical(Log::Bitcoin) << "Verifying last" << nCheckDepth << "blocks at level" << nCheckLevel;

    CBlockIndex* pindexState = blockchain.Tip();
    CBlockIndex* pindexFailure = nullptr;
    int nGoodTransactions = 0;
    for (CBlockIndex* pindex = blockchain.Tip(); pindex && pindex->pprev; pindex = pindex->pprev) {
        boost::this_thread::interruption_point();
        uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99,
                (int)(((double)(globalChain->Height() - pindex->nHeight)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100)))));

        if (pindex->nHeight < globalChain->Height()-nCheckDepth)
            break;
        auto future = validator.addBlock(pindex->GetBlockPos(), 0);
        future.setOnlyCheckValidity(true);
        future.setCheckTransactionValidity(nCheckLevel >= 1);
        future.start();
        future.waitUntilFinished();
        if (!future.error().empty()) {
            logFatal(Log::Bitcoin) << "VerifyDB failed validate block at height" << pindex->nHeight << "Error is" << future.error();
            return false;
        }

        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState/* && (coins.DynamicMemoryUsage() + pcoinsTip->DynamicMemoryUsage()) <= nCoinCacheUsage*/) {
            bool fClean = true;
            auto fastBlock = Blocks::DB::instance()->loadBlock(pindex->GetBlockPos());
            try {
                fastBlock.findTransactions();
                if (!validator.disconnectTip(fastBlock, pindex, coins, &fClean))
                    return error("VerifyDB(): *** irrecoverable inconsistency in block data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            } catch (const std::runtime_error &e) {
                logDebug() << e;
                fClean = false;
            }

            pindexState = pindex->pprev;
            blockchain.SetTip(pindexState);
            if (!fClean) {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            } else {
                nGoodTransactions += fastBlock.transactions().size();
            }
        }
        if (ShutdownRequested())
            return true;
    }
    if (pindexFailure)
        return error("VerifyDB(): *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n",
                     blockchain.Height() - pindexFailure->nHeight + 1, nGoodTransactions);


    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4) {
        CBlockIndex *pindex = pindexState;
        while (pindex != globalChain->Tip()) {
            boost::this_thread::interruption_point();
            uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, 100 - (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * 50))));
            pindex = globalChain->Next(pindex);

            auto future = validator.addBlock(pindex->GetBlockPos(), 0).start();
            future.waitUntilFinished();
            if (!future.error().empty()) {
                logFatal(Log::Bitcoin) << "VerifyDB failed re-attach block at height" << pindex->nHeight << "Error is" << future.error();
                return false;
            }
        }
    }

    logCritical(Log::Bitcoin).nospace() << "No coin database inconsistencies in last " << chainActive.Height() - pindexState->nHeight
                                        << " blocks (" << nGoodTransactions << " transactions)";

    return true;
}
