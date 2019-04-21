/*
 * This file is part of the Flowee project
 * Copyright (C) 2017,2019 Tom Zander <tomz@freedommail.ch>
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
#include "FastUndoBlock.h"

#include <streaming/streams.h>
#include <undo.h>
#include <streaming/MessageBuilder.h>
#include <streaming/BufferPool.h>

enum UndoBlockSpec
{
    End = 0,
    StartBlock = 0x10, // with block-hash as arg
    /// an item that was inserted into the UTXO, that when undone will be removed
    RMTxId,
    RMTxOutIndex,
    /// an item that was deleted from the UTXO, that when undo will be re-inserted.
    InsTxId,
    InsTxOutIndex,
    InsBlockHeight,
    InsOffsetInBlock
};


FastUndoBlock::FastUndoBlock(const Streaming::ConstBuffer &rawBlock)
    : m_data(rawBlock),
      m_parser(m_data)
{
}

FastUndoBlock::Item FastUndoBlock::nextItem()
{
    FastUndoBlock::Item answer;
    auto type = m_parser.next();
    while (type == Streaming::FoundTag) {
        switch (m_parser.tag()) {
        case End:
            return answer;
        case StartBlock: break;
        case RMTxId:
            answer.prevTxId = m_parser.uint256Data();
            break;
        case RMTxOutIndex:
            answer.outputIndex = m_parser.intData();
            return answer;
        case InsTxId:
            answer.prevTxId = m_parser.uint256Data();
            break;
        case InsTxOutIndex:
            answer.outputIndex = m_parser.intData();
            break;
        case InsBlockHeight:
            answer.blockHeight = m_parser.intData();
            break;
        case InsOffsetInBlock:
            answer.offsetInBlock = m_parser.intData();
            return answer;
        default:
            assert(false);
            return answer;
        }
        type = m_parser.next();
    }
    return answer;
}

void FastUndoBlock::restartStream()
{
    m_parser = Streaming::MessageParser(m_data);
}

UndoBlockBuilder::UndoBlockBuilder(const uint256 &blockId, Streaming::BufferPool *pool)
    : m_pool(pool),
    m_ownsPool(m_pool == nullptr)
{
    if (m_ownsPool)
        m_pool = new Streaming::BufferPool();
    m_pool->reserve(40);
    Streaming::MessageBuilder builder(*m_pool);
    builder.add(StartBlock, blockId);
    m_data.push_back(builder.buffer());
}

UndoBlockBuilder::~UndoBlockBuilder()
{
    if (m_ownsPool)
        delete m_pool;
}

void UndoBlockBuilder::append(const std::deque<FastUndoBlock::Item> &items)
{
    m_pool->reserve(items.size() * 60);
    Streaming::MessageBuilder builder(*m_pool);
    for (auto item : items) {
        if (item.isInsert()) { // in undo that means we remember the reverse action
            builder.add(RMTxId, item.prevTxId);
            builder.add(RMTxOutIndex, item.outputIndex);
        } else {
            builder.add(InsTxId, item.prevTxId);
            builder.add(InsTxOutIndex, item.outputIndex);
            builder.add(InsBlockHeight, item.blockHeight);
            builder.add(InsOffsetInBlock, item.offsetInBlock);
        }
    }
    m_data.push_back(builder.buffer());
}

std::deque<Streaming::ConstBuffer> UndoBlockBuilder::finish() const
{
    return m_data;
}
