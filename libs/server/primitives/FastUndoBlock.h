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

#ifndef FLOWEE_PRIMITIVES_FASTUNDOBLOCK_H
#define FLOWEE_PRIMITIVES_FASTUNDOBLOCK_H

#include <streaming/ConstBuffer.h>
#include <streaming/MessageParser.h>
#include <undo.h>

#include <deque>
#include <vector>

class CBlockUndo;

class FastUndoBlock
{
public:
    struct Item {
        /// Create a new item that was inserted into the UTXO, that when undone will be removed
        Item (const uint256 &prevTxId, int outputIndex)
            : prevTxId(prevTxId), outputIndex(outputIndex) {}

        /// Create a new item that was deleted from the UTXO, that when undo will be re-inserted.
        Item (const uint256 &prevTxId, int outputIndex, int blockHeight, int offsetInBlock)
            : prevTxId(prevTxId), outputIndex(outputIndex), blockHeight(blockHeight), offsetInBlock(offsetInBlock) {}

        Item() {}

        bool isInsert() const {
            return blockHeight == -1;
        }
        bool isValid() const {
            return !prevTxId.IsNull();
        }
        uint256 prevTxId;
        int outputIndex = -1;
        int blockHeight = -1;
        int offsetInBlock = -1;
    };
    FastUndoBlock(const Streaming::ConstBuffer &rawBlock);
    FastUndoBlock(const FastUndoBlock &other) = default;

    /// return the total size of this block.
    inline int size() const {
        return m_data.size();
    }

    /// \internal
    inline Streaming::ConstBuffer data() const {
        return m_data;
    }

    Item nextItem();

private:
    Streaming::ConstBuffer m_data;
    Streaming::MessageParser m_parser;
};

class UndoBlockBuilder {
public:
    UndoBlockBuilder(const uint256 &blockId, Streaming::BufferPool *pool = nullptr);
    ~UndoBlockBuilder();

    void append(const std::deque<FastUndoBlock::Item> &items);

    std::deque<Streaming::ConstBuffer> finish() const;

private:
    Streaming::BufferPool *m_pool;
    std::deque<Streaming::ConstBuffer> m_data;
    bool m_ownsPool;
};

#endif
