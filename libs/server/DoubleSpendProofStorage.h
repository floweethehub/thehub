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
    /// notice that if the proof (by hash) was known, we return -1 instead.
    int add(const DoubleSpendProof &proof);
    /// remove by proof-id
    void remove(int proof);

    /// add()s and additionally registers the proof as an orphan.
    /// Orphans expire after secondsToKeepOrphans() elapses. They may
    /// be claimed using 'claimOrphan()'.
    void addOrphan(const DoubleSpendProof &proof, int peerId);
    /// Returns all (not yet verified) orphans matching prevOut.
    /// Each item is a pair of a proofId and the nodeId that send the proof to us.
    std::list<std::pair<int, int> > findOrphans(const COutPoint &prevOut) const;

    /// Flags the proof associated with hash as not an orphan, and thus
    /// not subject to automatic expiry.
    void claimOrphan(int proofId);

    /// Lookup a double-spend proof by id.
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
    std::map<int, std::pair<int, int64_t> > m_orphans;

    typedef boost::unordered_map<uint256, int, HashShortener> LookupTable;
    LookupTable m_dspIdLookupTable;
    std::map<uint64_t, std::deque<int> > m_prevTxIdLookupTable;
    mutable std::recursive_mutex m_lock;

    CRollingBloomFilter m_recentRejects;
};

#endif
