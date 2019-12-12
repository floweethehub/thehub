/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
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
#include "TxIndexer.h"
#include "Indexer.h"

#include <Message.h>
#include <APIProtocol.h>
#include <streaming/MessageParser.h>

TxIndexer::TxIndexer(boost::asio::io_service &service, const boost::filesystem::path &basedir, Indexer *datasource)
    : m_txdb(service, basedir),
      m_dataSource(datasource)
{
    UnspentOutputDatabase::setChangeCountCausesStore(50000);
}

int TxIndexer::blockheight() const
{
    return m_txdb.blockheight();
}

uint256 TxIndexer::blockId() const
{
    return m_txdb.blockId();
}

void TxIndexer::blockFinished(int blockheight, const uint256 &blockId)
{
    m_txdb.blockFinished(blockheight, blockId);
}

void TxIndexer::insert(const uint256 &txid, int blockHeight, int offsetInBlock)
{
    m_txdb.insert(txid, 0, blockHeight, offsetInBlock);
}

TxIndexer::TxData TxIndexer::find(const uint256 &txid) const
{
    TxData answer;
    auto item = m_txdb.find(txid, 0);
    if (item.isValid()) {
        answer.blockHeight = item.blockHeight();
        answer.offsetInBlock = item.offsetInBlock();
    }
    return answer;
}

void TxIndexer::run()
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
        uint256 txid;
        int blockHeight = -1;

        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::BlockHeight) {
                assert(blockHeight == -1);
                blockHeight = parser.intData();
                Q_ASSERT(blockHeight == m_txdb.blockheight() + 1);
            } else if (parser.tag() == Api::BlockChain::BlockHash) {
                blockId = parser.uint256Data();
            } else if (parser.tag() == Api::BlockChain::Separator) {
                if (txOffsetInBlock > 0 && !txid.IsNull()) {
                    assert(blockHeight > 0);
                    assert(blockHeight > m_txdb.blockheight());
                    m_txdb.insert(txid, 0, blockHeight, txOffsetInBlock);
                }
                txOffsetInBlock = 0;
            } else if (parser.tag() == Api::BlockChain::Tx_OffsetInBlock) {
                txOffsetInBlock = parser.intData();
            } else if (parser.tag() == Api::BlockChain::TxId) {
                txid = parser.uint256Data();
            }
        }
        assert(blockHeight > 0);
        assert(!blockId.IsNull());
        // in case the last one isn't followed with a Separator tag.
        if (txOffsetInBlock > 0 && !txid.IsNull()) {
            assert(blockHeight > 0);
            assert(blockHeight > m_txdb.blockheight());
            m_txdb.insert(txid, 0, blockHeight, txOffsetInBlock);
        }
        m_txdb.blockFinished(blockHeight, blockId);
        if (blockHeight == tipOfChain)
            m_txdb.saveCaches();
    }
}
