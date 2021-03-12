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
#ifndef BLOCKMETADATA_H
#define BLOCKMETADATA_H

#include <streaming/BufferPool.h>
#include <streaming/ConstBuffer.h>

#include <primitives/FastBlock.h>

#include <vector>
#include <deque>
#include <memory>

class BlockMetaData
{
public:
    /**
     * Constructor that loads the data from a buffer.
     * \see the static creation method parseBlock()
     */
    BlockMetaData(const Streaming::ConstBuffer &buffer);


    /**
     * Return true if this metadata object has proper fee data.
     * Since it is legal to construct a block meta data with no fees info and that simply
     * makes all the fees be set to zero (except for the coinbase one) it is helpful to
     * ask if fees are present.
     */
    bool hasFeesData() const;


    /**
     * The proper way to create a new BlockMetaData and fill it with data.
     *
     * The perTxFees are required to have a list of lists, which when we put them all together make one
     * list of fee items per transaction, skipping the coinbase. So total fee objects is one less than all the
     * transactions in a block since the coinbase is not represented here.
     * The perTxFees can be an empty vector if no fees are present.
     */
    static BlockMetaData parseBlock(int blockHeight, const FastBlock &block,
                                    const std::vector<std::unique_ptr<std::deque<std::int32_t> > > &perTxFees,
                                    Streaming::BufferPool &pool);

    /**
     * The per-transaction data.
     */
    struct TransactionData {
        char txid[32];
        uint32_t offsetInBlock;
        uint32_t fees : 24;
        uint32_t scriptTags : 8;

        const TransactionData *next() const {
            return this + 1;
        }
    };

    /**
     * Find a transaction by txid.
     * This will walk over the list of transactions and find a match with /a txid.
     * We return a nullptr if no match was found.
     */
    const TransactionData *findTransaction(const uint256 &txid) const;
    /**
     * Find a transaction by offset in block.
     * This will walk over the list of transactions and find a match with /a offsetInBlock.
     * We return a nullptr if no match was found.
     */
    const TransactionData *findTransaction(int offsetInBlock) const;

    /**
     * Return the first transaction data (aka the coinbase).
     */
    const TransactionData *first() const;

    /**
     * Returns the amount of transactions that are in this block.
     */
    int txCount() const;
    /**
     * Return the TransactionData by index.
     * Coinbase is transaction zero.
     */
    const TransactionData* tx(int index) const;

    /**
     * \internal
     * Returns the hard data.
     */
    Streaming::ConstBuffer data() const;

    /**
     * Returns the blockheight this block was mined at.
     */
    int blockHeight() const;

    /**
     * Returns true if the transactions are sorted by txid.
     */
    bool ctorSorted() const;

    /**
     * Return the block id (or blockheader-hash) for this block.
     */
    uint256 blockId() const;

    /// the scripting tags present in a transaction (any output).
    enum ScriptTags {
        OpReturn = 1,
        OpChecksig = 2, // including the verify version
        OpCheckmultisig = 4, // including the verify version
        OpCheckLockTimeverify = 8,
        OpCheckDataSig = 0x10, // including the verify version
        P2SH = 0x20
    };

private:
    bool m_ctorSorted = false;
    int m_blockHeight = 0;
    uint256 m_blockId;
    const Streaming::ConstBuffer m_data;
    Streaming::ConstBuffer m_transactions;
};

#endif
