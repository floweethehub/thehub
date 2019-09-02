/*
 * This file is part of the Flowee project
 * Copyright (c) 2019 Tom Zander <tomz@freedommail.ch>
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
#ifndef DOUBLESPENDPROOFSTORAGE_H
#define DOUBLESPENDPROOFSTORAGE_H

#include "DoubleSpendProof.h"
#include "bloom.h"
#include <boost/unordered_map.hpp>

#include <map>
#include <set>
#include <mutex>

class COutPoint;

class DoubleSpendProofStorage
{
public:
    DoubleSpendProofStorage();

    /// returns a double spend proof based on proof-id
    DoubleSpendProof proof(int proof) const;
    /// adds a proof, returns an internal proof-id that proof is known under.
    /// notice that if the proof (by hash) was known, that proof-id is returned instead.
    int add(const DoubleSpendProof &proof);
    /// remove by proof-id
    void remove(int proof);

    /// this add()s and additionally registers this is an orphan.
    /// you can fetch those upto 90s using 'claim()'.
    void addOrphan(const DoubleSpendProof &proof);
    /// returns -1 if not found, otherwise a proof-id
    int claimOrphan(const COutPoint &prevOut);

    DoubleSpendProof lookup(const uint256 &proofId) const;
    bool exists(const uint256 &proofId) const;

    // called every minute
    void periodicCleanup();

    bool isRecentlyRejectedProof(const uint256 &proofHash) const;
    void markProofRejected(const uint256 &proofHash);
    void newBlockFound();

private:
    std::map<int, DoubleSpendProof> m_proofs;
    int m_nextId = 1;
    std::map<int, int64_t> m_orphans;

    typedef boost::unordered_map<uint256, int, HashShortener> LookupTable;
    LookupTable m_dspIdLookupTable;
    std::map<uint64_t, std::deque<int> > m_prevTxIdLookupTable;
    mutable std::recursive_mutex m_lock;

    CRollingBloomFilter m_recentRejects;
};

#endif
