/*
 * This file is part of the Flowee project
 * Copyright (C) 2020 Tom Zander <tomz@freedommail.ch>
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
#include "PrivacySegment.h"
#include "DataListenerInterface.h"

#include <streaming/P2PBuilder.h>
#include <streaming/P2PParser.h>
#include <primitives/FastTransaction.h>
#include <primitives/pubkey.h>
#include <cashaddr.h>
#include <base58.h>

PrivacySegment::PrivacySegment(uint16_t id, DataListenerInterface *parent)
    : m_segmentId(id),
    m_bloom(10000, 0.001, rand(), BLOOM_UPDATE_ALL),
    m_parent(parent)
{
    assert(m_segmentId > 0); // zero is not allowed, that is the 'unset' value elsewhere
}

uint16_t PrivacySegment::segmentId() const
{
    return m_segmentId;
}

void PrivacySegment::clearFilter()
{
    m_bloom.clear();
}

void PrivacySegment::addToFilter(const uint256 &prevHash, uint32_t outIndex)
{
    std::vector<unsigned char> data;
    data.resize(36);
    memcpy(data.data(), prevHash.begin(), 32);
    WriteLE32(data.data() + 32, outIndex);
    m_bloom.insert(data);
}

void PrivacySegment::addToFilter(const std::string &address, int blockHeight)
{
    CashAddress::Content c = CashAddress::decodeCashAddrContent(address, "bitcoincash");
    if (c.hash.empty()) {
        CBase58Data old; // legacy address encoding
        if (old.SetString(address)) {
            c.hash = old.data();
            if (!old.isMainnetPkh() && !old.isMainnetSh()) {
                logCritical() << "PrivacySegment::addToFilter: Address could not be parsed";
                return;
            }
        }
    }
    m_bloom.insert(c.hash);

    if (blockHeight > 0) {
        if (m_firstBlock == -1)
            m_firstBlock = blockHeight;
        else
            m_firstBlock = std::min(m_firstBlock, blockHeight);
    }
}

void PrivacySegment::addToFilter(const CKeyID &address, int blockHeight)
{
    m_bloom.insert(std::vector<uint8_t>(address.begin(), address.end()));

    if (blockHeight > 0) {
        if (m_firstBlock == -1)
            m_firstBlock = blockHeight;
        else
            m_firstBlock = std::min(m_firstBlock, blockHeight);
    }
}

Streaming::ConstBuffer PrivacySegment::writeFilter(Streaming::BufferPool &pool) const
{
    pool.reserve(m_bloom.GetSerializeSize(0, 0));
    Streaming::P2PBuilder builder(pool);
    m_bloom.store(builder);
    return builder.buffer();
}

int PrivacySegment::firstBlock() const
{
    return m_firstBlock;
}

void PrivacySegment::blockSynched(int height)
{
    if (height <= m_merkleBlockHeight)
        m_softMerkleBlockHeight = height;
    else
        m_merkleBlockHeight = height;
}

int PrivacySegment::lastBlockSynched() const
{
    if (m_merkleBlockHeight == -1)
        return m_firstBlock - 1;
    return m_merkleBlockHeight;
}

int PrivacySegment::backupSyncHeight() const
{
    if (m_softMerkleBlockHeight == -1)
        return m_firstBlock - 1;
    return m_softMerkleBlockHeight;
}

void PrivacySegment::newTransactions(const BlockHeader &header, int blockHeight, const std::deque<Tx> &blockTransactions)
{
/*
 * Notice that the transactions match hit our filter, that doesn't mean it actually matched the
 * address or output that the wallet owns.
 * The wallet should thus test this and make sure that our filter is updated continuesly
 * with new outputs and replaced with a new filter when many outputs are already spent (which
 * we then want to push to peers to avoid them sending us some false-positives).
 */
    m_parent->newTransactions(header, blockHeight, blockTransactions);
}

void PrivacySegment::newTransaction(const Tx &tx)
{
    m_parent->newTransaction(tx);
}

int PrivacySegment::filterChangedHeight() const
{
    return m_filterChangedHeight;
}

CBloomFilter PrivacySegment::bloomFilter() const
{
    return m_bloom;
}
