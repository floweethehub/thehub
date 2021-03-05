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


class BlockMetaData
{
public:
    enum ScriptTags {
        OpReturn = 1,
        OpChecksig = 2, // including the verify version
        OpCheckmultisig = 4, // including the verify version
        OpCheckLockTimeverify = 8,
        OpCheckDataSig = 0x10, // including the verify version
        P2SH = 0x20
    };

    BlockMetaData(const Streaming::ConstBuffer &buffer);

    static BlockMetaData parseBlock(int blockHeight, const FastBlock &block, Streaming::BufferPool &pool);

    struct TransactionData {
        char txid[32];
        uint32_t offsetInBlock;
        uint32_t fees : 24;
        uint32_t scriptTags : 8;

        const TransactionData *next() const {
            return this + 1;
        }
    };

    const TransactionData *first() const;

    int txCount() const;
    const TransactionData* tx(int index) const;

    Streaming::ConstBuffer data() const;

    int blockHeight() const;

    bool ctorSorted() const;

private:
    bool m_ctorSorted = false;
    int m_blockHeight = 0;
    const Streaming::ConstBuffer m_data;
    Streaming::ConstBuffer m_transactions;
};

#endif
