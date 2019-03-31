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
#include <qbytearray.h>
#include <primitives/pubkey.h>

Indexer::Indexer(const boost::filesystem::path &basedir)
    : m_txdb(m_workers.ioService(), basedir / "txindex"),
    m_addressdb(basedir / "addresses"),
    m_network(m_workers.ioService())
{
    // TODO add some auto-save of the databases.

    uint256 txid = uint256S("0e3e2357e806b6cdb1f70b54c3a3a17b6714ee1f0e68bebb44a74b1efd512098");
    auto item = m_txdb.find(txid);
    logFatal() << "Found txid at height" << item.blockHeight << "pos:" << item.offsetInBlock;

    QByteArray data = QByteArray::fromHex("BB7F084E57F4F250CAB45F73C62841F83BF1D5F6");
    const uint160 *address = reinterpret_cast<uint160*>(data.data());
    // auto item = m_txdb.find(txid);
    std::vector<AddressIndexer::TxData> x = m_addressdb.find(*address);
    for (auto i : x) {
        logFatal() << "Address" << data.toHex().toStdString() << "found in tx in block" << i.blockHeight
                   << "offsetinblock" << i.offsetInBlock
                   << "output" << i.outputIndex;
    }
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

    int blockHeight = std::min(m_txdb.blockheight(), m_addressdb.blockheight());
    logCritical() << "Connection to hub established, highest block we know:" << blockHeight
        << "requesting next";
    requestBlock(blockHeight + 1);
}

void Indexer::requestBlock(int height)
{
    m_pool.reserve(20);
    Streaming::MessageBuilder builder(m_pool);
    builder.add(Api::BlockChain::BlockHeight, height);
    if (m_txdb.blockheight() < height)
        builder.add(Api::BlockChain::GetBlock_TxId, true);
    if (m_addressdb.blockheight() < height)
        builder.add(Api::BlockChain::GetBlock_OutputAddresses, true);
    builder.add(Api::BlockChain::GetBlock_OffsetInBlock, true);
    m_serverConnection.send(builder.message(Api::BlockChainService, Api::BlockChain::GetBlock));
}

void Indexer::hubDisconnected()
{
    logCritical() << "Hub disconnected";
}

void Indexer::hubSentMessage(const Message &message)
{
    if (message.serviceId() == Api::BlockChainService) {
        if (message.messageId() == Api::BlockChain::GetBlockReply) {
            int newHeight = processNewBlock(message);
            requestBlock(newHeight + 1);
            if (newHeight % 500 == 0)
                logDebug() << "Finished block" << newHeight;
        }
    }
    else if (message.serviceId() == Api::FailuresService && message.messageId() == Api::Failures::CommandFailed) {
        Streaming::MessageParser parser(message.body());
        int serviceId = -1;
        int messageId = -1;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::Failures::FailedCommandServiceId)
                serviceId = parser.intData();
            else if (parser.tag() == Api::Failures::FailedCommandId)
                messageId = parser.intData();
        }
        if (serviceId == Api::BlockChainService && messageId == Api::BlockChain::GetBlock) {
            logCritical() << "Failed to get block, assuming we are at 'top' of chain";
        }
    }
    else {
        Streaming::MessageParser::debugMessage(message);
    }
}

int Indexer::processNewBlock(const Message &message)
{
    int txOffsetInBlock = 0;
    int outputIndex = -1;
    uint256 blockId;
    uint256 txid;
    Streaming::MessageParser parser(message.body());
    int blockHeight = -1;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::BlockChain::BlockHeight) {
            if (blockHeight != -1) Streaming::MessageParser::debugMessage(message);
            assert(blockHeight == -1);
            blockHeight = parser.intData();
        } else if (parser.tag() == Api::BlockChain::BlockHash) {
            blockId = parser.uint256Data();
        } else if (parser.tag() == Api::BlockChain::Separator) {
            if (txOffsetInBlock > 0 && !txid.IsNull()) {
                assert(blockHeight > 0);
                assert(blockHeight > m_txdb.blockheight());
                m_txdb.insert(txid, blockHeight, txOffsetInBlock);
            }
            txOffsetInBlock = 0;
            outputIndex = -1;
        } else if (parser.tag() == Api::BlockChain::Tx_OffsetInBlock) {
            txOffsetInBlock = parser.intData();
        } else if (parser.tag() == Api::BlockChain::TxId) {
            txid = parser.uint256Data();
        } else if (parser.tag() == Api::BlockChain::Tx_Out_Index) {
            outputIndex = parser.intData();
        } else if (parser.tag() == Api::BlockChain::Tx_Out_Address) {
            assert(parser.dataLength() == 20);
            assert(outputIndex >= 0);
            assert(blockHeight > 0);
            assert(txOffsetInBlock > 0);
            m_addressdb.insert(parser.bytesDataBuffer(), outputIndex, blockHeight, txOffsetInBlock);
        }
    }
    assert(blockHeight > 0);
    assert(!blockId.IsNull());
    m_txdb.blockFinished(blockHeight, blockId);
    m_addressdb.blockFinished(blockHeight, blockId);
    return blockHeight;
}
