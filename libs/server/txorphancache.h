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
#ifndef TXORPHANCACHE_H
#define TXORPHANCACHE_H

#include "sync.h"
#include "primitives/transaction.h"

class CTxOrphanCache
{
public:
    CTxOrphanCache();
    static CTxOrphanCache *instance();

    struct COrphanTx {
        CTransaction tx;
        int fromPeer;
        uint64_t nEntryTime;
        uint32_t onResultFlags;
    };
    bool addOrphanTx(const CTransaction& tx, int peerId, uint32_t onResultFlags = 0, uint64_t originalEntryTime = 0);

    void eraseOrphansByTime();

    std::uint32_t limitOrphanTxSize();

    inline const std::map<uint256, COrphanTx> & mapOrphanTransactions() const {
        return m_mapOrphanTransactions;
    }

    static void clear();
    static bool value(const uint256 &txid, CTransaction &output);
    static bool contains(const uint256 &txid);

    std::vector<uint256> fetchTransactionIds() const;

    void setLimit(std::uint32_t limit);

    std::vector<COrphanTx> fetchTransactionsByPrev(const uint256 &txid) const;

    void eraseOrphans(const std::vector<uint256> &txIds);

protected:
    mutable CCriticalSection m_lock;
    std::map<uint256, COrphanTx> m_mapOrphanTransactions;
    std::map<uint256, std::set<uint256> > m_mapOrphanTransactionsByPrev;

    static CTxOrphanCache *s_instance;

    // this one doesn't lock!
    void eraseOrphanTx(uint256 hash);
    uint32_t limitOrphanTxSize(uint32_t nMaxOrphans);

private:
    std::uint32_t m_limit;
};

#endif // TXORPHANCACHE_H
