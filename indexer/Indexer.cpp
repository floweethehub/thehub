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
#include "Indexer.h"

#include <Logger.h>
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>
#include <APIProtocol.h>

Indexer::Indexer(const boost::filesystem::path &basedir)
    : m_txdb(m_workers.ioService(), basedir / "txindex"),
    m_network(m_workers.ioService())
{
    // TODO add some auto-save of the databases.
}

Indexer::~Indexer()
{
    m_serverConnection.disconnect();
}

void Indexer::tryConnectHub(const EndPoint &ep)
{
    m_serverConnection = std::move(m_network.connection(ep));
    if (!m_serverConnection.isValid())
        throw std::runtime_error("Invalid Endpoint, can't create connection");
    m_serverConnection.setOnConnected(std::bind(&Indexer::hubConnected, this, std::placeholders::_1));
    m_serverConnection.setOnDisconnected(std::bind(&Indexer::hubDisconnected, this));
    m_serverConnection.setOnIncomingMessage(std::bind(&Indexer::hubSentMessage, this, std::placeholders::_1));
    m_serverConnection.connect();
}

void Indexer::hubConnected(const EndPoint &ep)
{

    int blockHeight = m_txdb.blockheight();
    logInfo() << "Connection to hub established, highest block we know:" << blockHeight
        << "requesting next";
    requestBlock(blockHeight + 1);
}

void Indexer::requestBlock(int height)
{
    m_pool.reserve(20);
    Streaming::MessageBuilder builder(m_pool);
    builder.add(Api::BlockChain::BlockHeight, height);
    builder.add(Api::BlockChain::GetBlock_TxId, true);
    builder.add(Api::BlockChain::GetBlock_OffsetInBlock, true);
    m_serverConnection.send(builder.message(Api::BlockChainService, Api::BlockChain::GetBlock));
}

void Indexer::hubDisconnected()
{
    logInfo() << "Hub disconnected";
}

void Indexer::hubSentMessage(const Message &message)
{
    if (message.serviceId() == Api::BlockChainService) {
        if (message.messageId() == Api::BlockChain::GetBlockReply) {
            int newHeight = processNewBlock(message);
            requestBlock(newHeight + 1);
            if (newHeight % 1000 == 0)
                logDebug() << "Finished block" << newHeight;
        }
    }
    else {
        Streaming::MessageParser::debugMessage(message);
    }
}

int Indexer::processNewBlock(const Message &message)
{
    int txOffsetInBlock = 0;
    uint256 blockId;
    uint256 txid;
    Streaming::MessageParser parser(message.body());
    int blockHeight = -1;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::BlockChain::BlockHeight) {
            blockHeight = parser.intData();
        } else if (parser.tag() == Api::BlockChain::BlockHash) {
            blockId = parser.uint256Data();
        } else if (parser.tag() == Api::BlockChain::Separator) {
            if (txOffsetInBlock > 0 && !txid.IsNull()) {
                assert(blockHeight > 0);
                assert(!txid.IsNull());
                m_txdb.insert(txid, blockHeight, txOffsetInBlock);
            }
            txOffsetInBlock = 0;
        } else if (parser.tag() == Api::BlockChain::Tx_OffsetInBlock) {
            txOffsetInBlock = parser.intData();
        } else if (parser.tag() == Api::BlockChain::TxId) {
            txid = parser.uint256Data();
        /*} else if (parser.tag() == Api::BlockChain::Tx_Out_Address) {
            address = base_blob<160>(parser.bytesDataBuffer().begin());
            if (txOffsetInBlock > 0) {
            }
            */
        }
    }
    assert(blockHeight > 0);
    assert(!blockId.IsNull());
    m_txdb.blockFinished(blockHeight, blockId);
    return blockHeight;
}
