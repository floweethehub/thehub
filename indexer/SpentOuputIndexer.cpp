/*
 * This file is part of the Flowee project
 * Copyright (C) 2019-2021 Tom Zander <tom@flowee.org>
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
#include "SpentOuputIndexer.h"
#include "Indexer.h"

#include <Message.h>
#include <APIProtocol.h>
#include <streaming/MessageParser.h>

SpentOutputIndexer::SpentOutputIndexer(boost::asio::io_service &service, const boost::filesystem::path &basedir, Indexer *indexer)
    : m_txdb(service, basedir),
      m_dataSource(indexer)
{
    UnspentOutputDatabase::setChangeCountCausesStore(50000);
    assert(indexer);
}

int SpentOutputIndexer::blockheight() const
{
    return m_txdb.blockheight();
}

uint256 SpentOutputIndexer::blockId() const
{
    return m_txdb.blockId();
}

void SpentOutputIndexer::blockFinished(int blockheight, const uint256 &blockId)
{
    m_txdb.blockFinished(blockheight, blockId);
}

void SpentOutputIndexer::insertSpentTransaction(const uint256 &prevTxId, int prevOutIndex, int blockHeight, int offsetInBlock)
{
    m_txdb.insert(prevTxId, prevOutIndex, blockHeight, offsetInBlock);
}

SpentOutputIndexer::TxData SpentOutputIndexer::findSpendingTx(const uint256 &txid, int output) const
{
    TxData answer;
    auto item = m_txdb.find(txid, output);
    if (item.isValid()) {
        answer.blockHeight = item.blockHeight();
        answer.offsetInBlock = item.offsetInBlock();
    }
    return answer;
}


void SpentOutputIndexer::run()
{
    assert(m_dataSource);
    while (!isInterruptionRequested()) {
        logDebug() << "want block" << m_txdb.blockheight() + 1;
        int tipOfChain;
        Message message = m_dataSource->nextBlock(m_txdb.blockheight() + 1, &tipOfChain);
        if (message.body().size() == 0)
            continue;
        int txOffsetInBlock = 0;
        uint256 blockId;
        int blockHeight = -1;

        uint256 prevTxId;
        bool gotPrevTxId = false;
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::BlockHeight) {
                blockHeight = parser.intData();
                assert(blockHeight == m_txdb.blockheight() + 1);
            } else if (parser.tag() == Api::BlockChain::BlockHash) {
                blockId = parser.uint256Data();
            } else if (parser.tag() == Api::BlockChain::Separator) {
                txOffsetInBlock = 0;
            } else if (parser.tag() == Api::BlockChain::Tx_OffsetInBlock) {
                txOffsetInBlock = parser.intData();
            } else if (txOffsetInBlock > 90 && parser.tag() == Api::BlockChain::Tx_IN_TxId) {
                prevTxId = parser.uint256Data();
                gotPrevTxId = true;
            } else if (txOffsetInBlock > 90 && parser.tag() == Api::BlockChain::Tx_IN_OutIndex) {
                assert(gotPrevTxId);
                gotPrevTxId = false;
                assert(!prevTxId.IsNull());
                assert(parser.isInt());
                assert(blockHeight >= 0);
                assert(txOffsetInBlock > 80);
                m_txdb.insert(prevTxId, parser.intData(), blockHeight, txOffsetInBlock);
            }
        }
        assert(blockHeight > 0);
        assert(!blockId.IsNull());
        m_txdb.blockFinished(blockHeight, blockId);
        if (blockHeight == tipOfChain)
            m_txdb.saveCaches();
    }
}
