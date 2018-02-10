/*
 * This file is part of the Flowee project
 * Copyright (c) 2009-2010 Satoshi Nakamoto
 * Copyright (c) 2009-2015 The Bitcoin Core developers
 * Copyright (c) 2017 Tom Zander <tomz@freedommail.ch>
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

uint32_t GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
uint32_t CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);
uint32_t CalculateNextCashWorkRequired(const CBlockIndex *pindexPrev, const CBlockHeader *pblock, const Consensus::Params &params);

/**
 * Check whether a block hash satisfies the proof-of-work requirement specified by nBits
 */
bool CheckProofOfWork(uint256 hash, uint32_t nBits, const Consensus::Params &);
arith_uint256 GetBlockProof(const CBlockIndex& block);

/** Return the time it would take to redo the work difference between from and to, assuming the current hashrate corresponds to the difficulty at tip, in seconds. */
int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params&);

#endif
