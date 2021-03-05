/*
 * This file is part of the Flowee project
 * Copyright (C) 2021 Tom Zander <tom@flowee.org>
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
#include "BlockMetaData.h"

#include <deque>
#include <string.h>

#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

enum Tags {
    BlockHeight,
    IsCTOR,
    TransactionDataBlob
};

BlockMetaData::BlockMetaData(const Streaming::ConstBuffer &buffer)
    : m_data(buffer)
{
    Streaming::MessageParser parser(buffer);
    while (parser.next() != Streaming::EndOfDocument) {
        if (parser.tag() == BlockHeight)
            m_blockHeight = parser.intData();
        else if (parser.tag() == IsCTOR)
            m_ctorSorted = parser.boolData();
        else if (parser.tag() == TransactionDataBlob) {
            assert(parser.isByteArray());
            m_transactions = parser.bytesDataBuffer();
        }
    }
}

BlockMetaData BlockMetaData::parseBlock(int blockHeight, const FastBlock &block, Streaming::BufferPool &pool)
{
    std::deque<TransactionData> txs;
    Tx::Iterator iter(block);
    uint64_t txOutputAmounts = 0;
    uint64_t txInputAmounts = 0;
    bool endFound = false;
    bool isCTOR = true;
    TransactionData currentTx;
    uint256 prevTxid;
    while (iter.next()) {
        if (iter.tag() == Tx::End) {
            if (endFound) // done
                break;
            Tx tx = iter.prevTx();
            uint256 txid = tx.createHash();
            memcpy(currentTx.txid, txid.begin(), 32);
            currentTx.offsetInBlock = tx.offsetInBlock(block);
txInputAmounts = txOutputAmounts + 4; // TODO remove
            assert (txOutputAmounts <= txInputAmounts) ;
            uint64_t fees = txInputAmounts - txOutputAmounts;
            if (fees < 0xFFFFFF) // fits in our field
                currentTx.fees = fees;
            else
                currentTx.fees = 0xFFFFFF; // -1, fee too heigh for our cache
            txs.push_back(currentTx);

            txInputAmounts = 0;
            txOutputAmounts = 0;
            currentTx.scriptTags = 0;

            if (txs.size() >= 2) {
                if (isCTOR && txs.size() > 2)
                    isCTOR = txid.Compare(prevTxid) > 0;
                if (isCTOR)
                    prevTxid = txid;
            }
            endFound = true;
            continue;
        }
        endFound = false;
        if (iter.tag() == Tx::OutputValue) {
            txOutputAmounts += iter.longData();
        }
        else if (iter.tag() == Tx::OutputScript) {
            // TODO fill the bit-field.
            currentTx.scriptTags = 0;
        }
    }

    pool.reserve(txs.size() * 40);
    for (size_t i = 0; i < txs.size(); ++i) {
        static_assert(sizeof(TransactionData) == 40, "Jump table row size");
        memcpy(pool.begin() + i * 40, &txs[i], 40);
    }
    auto txData = pool.commit(txs.size() * 40);

    pool.reserve(txData.size() + 20);
    Streaming::MessageBuilder builder(pool);
    builder.add(BlockHeight, blockHeight);
    builder.add(IsCTOR, isCTOR);
    builder.add(TransactionDataBlob, txData);

    return BlockMetaData(pool.commit());
}

const BlockMetaData::TransactionData *BlockMetaData::first() const
{
    return reinterpret_cast<const BlockMetaData::TransactionData*>(m_transactions.begin());
}

int BlockMetaData::txCount() const
{
    return m_transactions.size() / 40;
}

const BlockMetaData::TransactionData *BlockMetaData::tx(int index) const
{
    assert(index >= 0);
    auto *ptr = m_transactions.begin() + index * sizeof(TransactionData);
    if (ptr > m_transactions.end() - sizeof(TransactionData))
        throw std::runtime_error("Index out of bounds");
    return reinterpret_cast<const BlockMetaData::TransactionData*>(ptr);
}

Streaming::ConstBuffer BlockMetaData::data() const
{
    return m_data;
}

int BlockMetaData::blockHeight() const
{
    return m_blockHeight;
}

bool BlockMetaData::ctorSorted() const
{
    return m_ctorSorted;
}
