/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2015 The Bitcoin Core developers
 * Copyright (C) 2016-2021 Tom Zander <tom@flowee.org>
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

#ifndef FLOWEE_MINER_H
#define FLOWEE_MINER_H

#include "primitives/block.h"

#include <mutex>

#include <boost/thread.hpp>

class CBlockIndex;
namespace Validation { class Engine; }
class CChainParams;
class CReserveKey;
class CScript;
class CWallet;
namespace Consensus { struct Params; }

struct CBlockTemplate
{
    CBlock block;
    std::vector<int64_t> vTxFees;
};


class Mining
{
public:
    /**
     * parse /a the public address and return a script used to make it
     * a coinbase.
     * @throws runtime_error when the input is not usable
     */
    static CScript ScriptForCoinbase(const std::string &publicAddress);
    /** Run the miner threads */
    static void GenerateBitcoins(bool fGenerate, int nThreads, const CChainParams& chainparams, const std::string &GetCoinbase);
    static void Stop();

    static Mining *instance();
    Mining();
    ~Mining();

    /**
     * Generate a new block, without valid proof-of-work, using the global settings
     */
    CBlockTemplate* CreateNewBlock() const;
    /** Generate a new block, without valid proof-of-work */
    CBlockTemplate* CreateNewBlock(Validation::Engine &validationEngine) const;
    /** Modify the extranonce in a block */
    void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce);
    static int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev);

    CScript GetCoinbase() const;
    void SetCoinbase(const CScript &coinbase);

private:
    boost::thread_group* m_minerThreads;
    static Mining *s_instance;
    mutable std::mutex m_lock;
    CScript m_coinbase;
    std::vector<unsigned char> m_coinbaseComment;

    uint256 m_hashPrevBlock;
};

#endif
