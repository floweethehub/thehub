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
#ifndef PRIVACYSEGMENT_H
#define PRIVACYSEGMENT_H

#include "BlockHeader.h"

#include <utils/PartialMerkleTree.h>
#include <utils/bloom.h>

#include <deque>

class CKeyID;
class Tx;
class Message;
class DataListenerInterface;

/**
 * A wallet can split its funds into different privacy segments.
 * The effect is that backing resources will be allocated for each
 * segment and details will be cordoned off.
 *
 * A bloom filter, for instance, is known to allow combining of addresses
 * with higher probablity than we initially thought.
 * The simple solution to this is to not use the same bloom filter for
 * addresses that should be separated.
 *
 * The privacy segment is intended to be assigned to a certain set of
 * addresses in the wallet and the P2PNet library makes sure that we never
 * mix the segments when talking to the individual peers on the Bitcoin network.
 */
class PrivacySegment
{
public:
    explicit PrivacySegment(uint16_t id, DataListenerInterface *parent = nullptr);

    uint16_t segmentId() const;

    /// clears the bloom filter, to allow adding addresses and outputs to it again.
    void clearFilter();

    void addToFilter(const uint256 &prevHash, uint32_t outIndex);
    /**
     * @brief addToFilter allows you to get updates for a specific address.
     * @param address The address to add.
     * @param blockHeight the blockHeight the address was created at, first one we look at to get updates for data.
     */
    void addToFilter(const std::string &address, int blockHeight);
    /**
     * Convenience overload for the above method.
     */
    void addToFilter(const CKeyID &address, int blockHeight);

    Streaming::ConstBuffer writeFilter(Streaming::BufferPool &pool) const;

    int firstBlock() const;

    /// set the block a peer just synchronized (received and verified)
    void blockSynched(int height);
    /// returns the last block that was synched
    int lastBlockSynched() const;
    /// a backup peer doing a second sync has reached this height
    int backupSyncHeight() const;

    /**
     * @brief newTransactions announces a list of transactions pushed to us from a peer.
     * @param header the block header these transactions appeared in.
     * @param blockHeight the blockheight we know the header under.
     * @param blockTransactions The actual transactions.
     */
    void newTransactions(const BlockHeader &header, int blockHeight, const std::deque<Tx> &blockTransactions);
    /// A single transaction that matches our filters, forwarded to us as it hits a mempool.
    void newTransaction(const Tx &tx);

    int filterChangedHeight() const;

    CBloomFilter bloomFilter() const;

private:
    uint16_t m_segmentId = 0;
    int m_firstBlock = -1; ///< first block we need to investigate
    CBloomFilter m_bloom;
    DataListenerInterface *m_parent;
    int m_merkleBlockHeight = -1;
    int m_filterChangedHeight = 0;
    int m_softMerkleBlockHeight = -1;
};

#endif
