/*
 * This file is part of the Flowee project
 * Copyright (c) 2009-2010 Satoshi Nakamoto
 * Copyright (c) 2009-2015 The Bitcoin Core developers
 * Copyright (c) 2017 Tom Zander <tom@flowee.org>
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

#ifndef FLOWEE_POW_H
#define FLOWEE_POW_H

#include "consensus/params.h"

#include <cstdint>

class CBlockHeader;
class CBlockIndex;
class uint256;
class arith_uint256;

uint32_t CalculateNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
// Satoshi's algo. The 2016 block one.
uint32_t Calculate2016NextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);
// the difficulty algo we used in BCH from 15 November 2017 till, 15 Nov 2020.
uint32_t CalculateNextCW144WorkRequired(const CBlockIndex *pindexPrev, const CBlockHeader *pblock, const Consensus::Params &params);

// \internal
arith_uint256 CalculateASERT(const arith_uint256 &refTarget, const int64_t nPowTargetSpacing, const int64_t nTimeDiff,
        const int64_t nHeightDiff, const arith_uint256 &powLimit, const int64_t nHalfLife) noexcept;
// temporary hackish method until we have an actual blockheight for the anchor block, after the actual fork.
void ResetASERTAnchorBlockCache();

/**
 * Compute the next required proof of work using an absolutely scheduled
 * exponentially weighted target (ASERT).
 *
 * With ASERT, we define an ideal schedule for block issuance (e.g. 1 block every 600 seconds), and we calculate the
 * difficulty based on how far the most recent block's timestamp is ahead of or behind that schedule.
 * We set our targets (difficulty) exponentially. For every [nHalfLife] seconds ahead of or behind schedule we get, we
 * double or halve the difficulty.
 */
uint32_t CalculateNextASERTWorkRequired(const CBlockIndex *pindexPrev, const CBlockHeader *pblock,
        const Consensus::Params &params, const CBlockIndex *pindexAnchorBlock);
/**
 * Check whether a block hash satisfies the proof-of-work requirement specified by nBits
 */
bool CheckProofOfWork(const uint256 &hash, uint32_t nBits, const Consensus::Params &);
arith_uint256 GetBlockProof(const CBlockIndex& block);

/** Return the time it would take to redo the work difference between from and to, assuming the current hashrate corresponds to the difficulty at tip, in seconds. */
int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params&);
#endif
