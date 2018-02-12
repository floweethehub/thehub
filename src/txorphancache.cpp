/*
 * This file is part of the Flowee project
 * Copyright (C) 2017 Tom Zander <tomz@freedommail.ch>
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
#include "txorphancache.h"
#include <SettingsDefaults.h>
#include "main.h"

#include "util.h"
#include "net.h"
#include <boost/foreach.hpp>

CTxOrphanCache::CTxOrphanCache()
    : m_limit(Settings::DefaultMaxOrphanTransactions)
{
}

CTxOrphanCache* CTxOrphanCache::s_instance = 0;
CTxOrphanCache* CTxOrphanCache::instance()
{
    if (s_instance == 0)
        s_instance = new CTxOrphanCache();
    return s_instance;
}

bool CTxOrphanCache::AddOrphanTx(const CTransaction& tx, NodeId peer, uint32_t onResultFlags, uint64_t originalEntryTime)
{
    LOCK(m_lock);

    uint256 hash = tx.GetHash();
    if (m_mapOrphanTransactions.count(hash))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // 5000 orphans, each of which is at most 100,000 bytes big is
    // at most 500 megabytes of orphans:

    unsigned int sz = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);
    if (sz > 100000) {
        logDebug(Log::Mempool) << "ignoring large orphan tx. Size:" << sz << "hash:" << hash.ToString();
        return false;
    }

    COrphanTx &entry = m_mapOrphanTransactions[hash];
    entry.tx = tx;
    entry.fromPeer = peer;
    if (originalEntryTime == 0)
        originalEntryTime = GetTime();
    entry.nEntryTime = originalEntryTime;
    entry.onResultFlags = onResultFlags;
    BOOST_FOREACH(const CTxIn& txin, tx.vin) {
        m_mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);
    }

    logDebug(Log::Mempool) << "stored orphan tx" << hash << "(mapsz"
        << m_mapOrphanTransactions.size() << "prevsz " << m_mapOrphanTransactionsByPrev.size() << ')';
    return true;
}

void CTxOrphanCache::EraseOrphanTx(uint256 hash)
{
    std::map<uint256, COrphanTx>::iterator it = m_mapOrphanTransactions.find(hash);
    if (it == m_mapOrphanTransactions.end())
        return;
    BOOST_FOREACH(const CTxIn& txin, it->second.tx.vin) {
        auto itPrev = m_mapOrphanTransactionsByPrev.find(txin.prevout.hash);
        if (itPrev == m_mapOrphanTransactionsByPrev.end())
            continue;
        itPrev->second.erase(hash);
        if (itPrev->second.empty())
            m_mapOrphanTransactionsByPrev.erase(itPrev);
    }
    m_mapOrphanTransactions.erase(it);
}

void CTxOrphanCache::EraseOrphansByTime()
{
    LOCK(m_lock);
    static int64_t nLastOrphanCheck = GetTime();

    // Because we have to iterate through the entire orphan cache which can be large we don't want to check this
    // every time a tx enters the mempool but just once every 5 minutes is good enough.
    if (GetTime() <  nLastOrphanCheck + 5 * 60)
        return;
    int64_t nOrphanTxCutoffTime = GetTime() - GetArg("-mempoolexpiry", Settings::DefaultMempoolExpiry) * 60 * 60;
    std::map<uint256, COrphanTx>::iterator iter = m_mapOrphanTransactions.begin();
    while (iter != m_mapOrphanTransactions.end()) {
        const auto entry = iter++;
        int64_t nEntryTime = entry->second.nEntryTime;
        if (nEntryTime < nOrphanTxCutoffTime) {
            uint256 txHash = entry->second.tx.GetHash();
            EraseOrphanTx(txHash);
            logDebug(Log::Mempool) << "Erased old orphan tx" << txHash << "of age" << (GetTime() - nEntryTime) << "seconds";
        }
    }

    nLastOrphanCheck = GetTime();
}

std::uint32_t CTxOrphanCache::LimitOrphanTxSize(std::uint32_t nMaxOrphans)
{
    LOCK(m_lock);
    unsigned int nEvicted = 0;
    while (m_mapOrphanTransactions.size() > nMaxOrphans) {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        std::map<uint256, COrphanTx>::iterator it = m_mapOrphanTransactions.lower_bound(randomhash);
        if (it == m_mapOrphanTransactions.end())
            it = m_mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}

uint32_t CTxOrphanCache::LimitOrphanTxSize()
{
    return LimitOrphanTxSize(m_limit);
}

void CTxOrphanCache::clear()
{
    if (s_instance) {
        LOCK(s_instance->m_lock);
        s_instance->m_mapOrphanTransactions.clear();
        s_instance->m_mapOrphanTransactionsByPrev.clear();
    }
}

bool CTxOrphanCache::value(const uint256 &txid, CTransaction &output)
{
    CTxOrphanCache *s = instance();
    LOCK(s->m_lock);
    auto iter = s->m_mapOrphanTransactions.find(txid);
    if (iter == s->m_mapOrphanTransactions.end())
        return false;
    output = iter->second.tx;
    return true;
}

bool CTxOrphanCache::contains(const uint256 &txid)
{
    CTxOrphanCache *s = instance();
    LOCK(s->m_lock);
    return s->m_mapOrphanTransactions.count(txid) > 0;
}

std::vector<uint256> CTxOrphanCache::fetchTransactionIds() const
{
    LOCK(m_lock);
    std::vector<uint256> answer;
    answer.reserve(m_mapOrphanTransactions.size());
    for (auto iter = m_mapOrphanTransactions.begin(); iter != m_mapOrphanTransactions.end(); ++iter)
        answer.push_back((*iter).first);
    return answer;
}

void CTxOrphanCache::setLimit(uint32_t limit)
{
    m_limit = limit;
}

std::vector<CTxOrphanCache::COrphanTx> CTxOrphanCache::fetchTransactionsByPrev(const uint256 &txid) const
{
    LOCK(m_lock);
    std::vector<CTxOrphanCache::COrphanTx> answer;
    auto itByPrev = m_mapOrphanTransactionsByPrev.find(txid);
    if (itByPrev == m_mapOrphanTransactionsByPrev.end())
        return answer;
    for (auto mi = itByPrev->second.begin(); mi != itByPrev->second.end(); ++mi) {
        const uint256& orphanHash = *mi;
        answer.push_back(m_mapOrphanTransactions.at(orphanHash));
    }
    return answer;
}

void CTxOrphanCache::EraseOrphans(const std::vector<uint256> &txIds)
{
    LOCK(m_lock);
    for (auto hashIter = txIds.begin(); hashIter != txIds.end(); ++hashIter) {
        auto it = m_mapOrphanTransactions.find(*hashIter);
        if (it == m_mapOrphanTransactions.end())
            continue;

        BOOST_FOREACH(const CTxIn& txin, it->second.tx.vin) {
            auto itPrev = m_mapOrphanTransactionsByPrev.find(txin.prevout.hash);
            if (itPrev == m_mapOrphanTransactionsByPrev.end())
                continue;
            itPrev->second.erase(*hashIter);
            if (itPrev->second.empty())
                m_mapOrphanTransactionsByPrev.erase(itPrev);
        }
        m_mapOrphanTransactions.erase(it);
    }
}
