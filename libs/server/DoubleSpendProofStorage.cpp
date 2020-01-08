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
#include "DoubleSpendProofStorage.h"
#include <utils/utiltime.h>
#include <primitives/transaction.h>

#define SECONDS_TO_KEEP_ORPHANS 90

DoubleSpendProofStorage::DoubleSpendProofStorage()
    : m_recentRejects(120000, 0.000001)
{
}

DoubleSpendProof DoubleSpendProofStorage::proof(int proof) const
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    auto iter = m_proofs.find(proof);
    if (iter != m_proofs.end())
        return iter->second;
    return DoubleSpendProof();
}

int DoubleSpendProofStorage::add(const DoubleSpendProof &proof)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);

    uint256 hash = proof.createHash();
    auto lookupIter = m_dspIdLookupTable.find(hash);
    if (lookupIter != m_dspIdLookupTable.end())
        return lookupIter->second;

    auto iter = m_proofs.find(m_nextId);
    while (iter != m_proofs.end()) {
        if (++m_nextId < 0)
            m_nextId = 1;
        iter = m_proofs.find(m_nextId);
    }
    m_proofs.insert(std::make_pair(m_nextId, proof));
    m_dspIdLookupTable.insert(std::make_pair(hash, m_nextId));

    return m_nextId++;
}

void DoubleSpendProofStorage::addOrphan(const DoubleSpendProof &proof)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    const int next = m_nextId;
    const int id = add(proof);
    if (id != next) // it was already in the storage
        return;

    m_orphans.insert(std::make_pair(id, GetTime()));
    m_prevTxIdLookupTable[proof.prevTxId().GetCheapHash()].push_back(id);
}

int DoubleSpendProofStorage::claimOrphan(const COutPoint &prevOut)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    auto iter = m_prevTxIdLookupTable.find(prevOut.hash.GetCheapHash());
    if (iter == m_prevTxIdLookupTable.end())
        return -1;

    std::deque<int> &q = iter->second;
    for (auto proofId = q.begin(); proofId != q.end(); ++proofId) {
        auto proofIter = m_proofs.find(*proofId);
        assert (proofIter != m_proofs.end());
        if (proofIter->second.prevOutIndex() != int(prevOut.n))
            continue;
        if (proofIter->second.prevTxId() == prevOut.hash) {
            q.erase(proofId);
            m_orphans.erase(*proofId);
            return *proofId;
        }
    }
    return -1;
}

void DoubleSpendProofStorage::remove(int proof)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    auto iter = m_proofs.find(proof);
    if (iter == m_proofs.end())
        return;

    auto orphan = m_orphans.find(iter->first);
    if (orphan != m_orphans.end()) {
        m_orphans.erase(orphan);
        auto orphanLookup = m_prevTxIdLookupTable.find(iter->second.prevTxId().GetCheapHash());
        if (orphanLookup != m_prevTxIdLookupTable.end()) {
            std::deque<int> &queue = orphanLookup->second;
            if (queue.size() == 1) {
                assert(queue.front() == proof);
                m_prevTxIdLookupTable.erase(orphanLookup);
            }
            else {
#ifndef NDEBUG
                size_t sizeBefore = queue.size();
#endif
                for (auto i = queue.begin(); i != queue.end(); ++i) {
                    if (*i == proof) {
                        queue.erase(i);
                        break;
                    }
                }
                assert(orphanLookup->second.size() < sizeBefore);
            }
        }
    }
    auto hash = iter->second.createHash();
    m_dspIdLookupTable.erase(hash);
    m_proofs.erase(iter);
}

DoubleSpendProof DoubleSpendProofStorage::lookup(const uint256 &proofId) const
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    auto lookupIter = m_dspIdLookupTable.find(proofId);
    if (lookupIter == m_dspIdLookupTable.end())
        return DoubleSpendProof();
    return m_proofs.at(lookupIter->second);
}

bool DoubleSpendProofStorage::exists(const uint256 &proofId) const
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    return m_dspIdLookupTable.find(proofId) != m_dspIdLookupTable.end();
}

void DoubleSpendProofStorage::periodicCleanup()
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    auto expire = GetTime() - SECONDS_TO_KEEP_ORPHANS;
    auto iter = m_orphans.begin();
    while (iter != m_orphans.end()) {
        if (iter->second <= expire) {
            const int proofId = iter->first;
            iter = m_orphans.erase(iter);
            remove(proofId);
        }
        else {
            ++iter;
        }
    }
    logDebug(Log::Mempool) << "DSP orphan count:" << m_orphans.size() << "DSProof count" << m_proofs.size();
}

bool DoubleSpendProofStorage::isRecentlyRejectedProof(const uint256 &proofHash) const
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    return m_recentRejects.contains(proofHash);
}

void DoubleSpendProofStorage::markProofRejected(const uint256 &proofHash)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    m_recentRejects.insert(proofHash);
}

void DoubleSpendProofStorage::newBlockFound()
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    m_recentRejects.reset();
}
