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

#include "IndexerClient.h"

#include <Logger.h>
#include <APIProtocol.h>
#include <qcoreapplication.h>

#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

IndexerClient::IndexerClient()
    : m_network(m_workers.ioService())
{
}

void IndexerClient::resolve(const QString &lookup)
{
    Q_ASSERT(m_indexConnection.isValid());
    if (lookup.size() == 64 || (lookup.size() == 66 && lookup.startsWith("0x"))) {
        uint256 hash = uint256S(lookup.toStdString().c_str());
        Streaming::MessageBuilder builder(Streaming::NoHeader, 40);
        builder.add(Api::Indexer::TxId, hash);
        m_indexConnection.send(builder.message(Api::IndexerService,
                                               Api::Indexer::FindTransaction));
    }
    // TODO address
}

void IndexerClient::tryConnectIndexer(const EndPoint &ep)
{
    m_indexConnection = std::move(m_network.connection(ep));
    if (!m_indexConnection.isValid())
        throw std::runtime_error("Invalid Endpoint, can't create connection");
#ifndef NDEBUG
    m_indexConnection.setOnConnected(std::bind(&IndexerClient::indexerConnected, this, std::placeholders::_1));
    m_indexConnection.setOnDisconnected(std::bind(&IndexerClient::indexerDisconnected, this));
#endif
    m_indexConnection.setOnIncomingMessage(std::bind(&IndexerClient::onIncomingIndexerMessage, this, std::placeholders::_1));
    m_indexConnection.connect();
}
void IndexerClient::tryConnectHub(const EndPoint &ep)
{
    m_hubConnection = std::move(m_network.connection(ep));
    if (!m_hubConnection.isValid())
        throw std::runtime_error("Invalid Endpoint, can't create connection");
#ifndef NDEBUG
    m_hubConnection.setOnConnected(std::bind(&IndexerClient::hubConnected, this, std::placeholders::_1));
    m_hubConnection.setOnDisconnected(std::bind(&IndexerClient::hubDisconnected, this));
#endif
    m_hubConnection.setOnIncomingMessage(std::bind(&IndexerClient::onIncomingHubMessage, this, std::placeholders::_1));
    m_hubConnection.connect();
}

void IndexerClient::hubConnected(const EndPoint &)
{
    logDebug() << "Hub connection established";
}

void IndexerClient::hubDisconnected()
{
    logDebug() << "Hub disconnected";
}

void IndexerClient::onIncomingHubMessage(const Message &message)
{

}

void IndexerClient::indexerConnected(const EndPoint &ep)
{
    logDebug() << "Indexer connection established";
}

void IndexerClient::indexerDisconnected()
{
    logDebug() << "Indexer disconnected";
    QCoreApplication::quit();
}

void IndexerClient::onIncomingIndexerMessage(const Message &message)
{
    if (message.serviceId() == Api::IndexerService) {
        if (message.messageId() == Api::Indexer::FindTransactionReply) {
            int blockHeight = -1, offsetInBlock = 0;
            Streaming::MessageParser parser(message);
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::Indexer::BlockHeight)
                    blockHeight = parser.intData();
                if (parser.tag() == Api::Indexer::OffsetInBlock)
                    offsetInBlock = parser.intData();
            }

            logFatal().nospace() << "Transaction location is: [block=" << blockHeight << "+" << offsetInBlock << "]";
            /*if (blockHeight > 0 && offsetInBlock > 80 && m_hubConnection.isValid()) {
                Streaming::MessageBuilder builder(Streaming::NoHeader, 20);
                builder.add(Api::BlockChain::BlockHeight, blockHeight);
                builder.add(Api::BlockChain::OffsetInBlock, offsetInBlock);
                m_hubConnection.send(builder.message(Api::BlockChainService, Api::BlockChain::GetTransaction));
            } else */
            QCoreApplication::quit();
        }
    }
}
