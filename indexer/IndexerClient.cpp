/*
 * This file is part of the Flowee project
 * Copyright (C) 2019-2020 Tom Zander <tom@flowee.org>
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

#include <base58.h>
#include <cashaddr.h>

namespace {
int digits(int number) {
    int rc = 1;
    while (number >= 10) {
        number /= 10;
        rc++;
    }
    return rc;
}
}

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

    CashAddress::Content c;
    CBase58Data old; // legacy address encoding
    if (old.SetString(lookup.toStdString())) {
        if (old.isMainnetPkh()) {
            Streaming::MessageBuilder builder(Streaming::NoHeader, 40);
            builder.addByteArray(Api::Indexer::BitcoinP2PKHAddress, old.data().data(), 20);
            m_indexConnection.send(builder.message(Api::IndexerService,
                                                   Api::Indexer::FindAddress));
        } else if (old.isMainnetSh()) {
            c.type = CashAddress::SCRIPT_TYPE;
            c.hash = old.data();
        } else {
            logCritical() << "Argument type not understood.";
            return;
        }
    }
    else {
        c = CashAddress::decodeCashAddrContent(lookup.toStdString(), "bitcoincash");
    }

    if (c.hash.size() == 20) {
        Streaming::MessageBuilder builder(Streaming::NoHeader, 40);
        builder.add(Api::Indexer::BitcoinScriptHashed, CashAddress::createHashedOutputScript(c));
        m_indexConnection.send(builder.message(Api::IndexerService, Api::Indexer::FindAddress));
    }
}

void IndexerClient::tryConnectIndexer(const EndPoint &ep)
{
    m_indexConnection = m_network.connection(ep);
    if (!m_indexConnection.isValid())
        throw std::runtime_error("Invalid Endpoint, can't create connection");
#ifndef NDEBUG
    m_indexConnection.setOnConnected(std::bind(&IndexerClient::indexerConnected, this, std::placeholders::_1));
#endif
    m_indexConnection.setOnDisconnected(std::bind(&IndexerClient::indexerDisconnected, this));
    m_indexConnection.setOnIncomingMessage(std::bind(&IndexerClient::onIncomingIndexerMessage, this, std::placeholders::_1));
    m_indexConnection.connect();
}

void IndexerClient::tryConnectHub(const EndPoint &ep)
{
    m_hubConnection = m_network.connection(ep);
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
    m_hubConnection.send(Message(Api::APIService, Api::Meta::Version));
}

void IndexerClient::hubDisconnected()
{
    logDebug() << "Hub disconnected";
}

void IndexerClient::onIncomingHubMessage(const Message &message)
{
    if (message.serviceId() == Api::BlockChainService) {
        if (message.messageId() == Api::BlockChain::GetTransactionReply) {
            Streaming::MessageParser parser(message);
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::BlockChain::GenericByteData) {
                    auto blob = parser.bytesDataBuffer();
                    QByteArray tx(blob.begin(), blob.size());
                    logCritical() << "Transaction follows. Tx-Size:" << tx.size() << "bytes";
                    if (tx.size() > 1500) {
                        logCritical() << "Large transaction. Use -v to display";
                        logInfo() << tx.toHex().constData();
                    }
                    else {
                        logCritical() << tx.toHex().constData();
                    }
                    QCoreApplication::quit();
                }
                else if (parser.tag() == Api::BlockChain::TxId) {
                    logCritical() << message.headerInt(Api::RequestId) << " -> " << parser.uint256Data();
                    if (--m_txIdsRequested == 0)
                        QCoreApplication::quit();
                }
            }
        }
    }
}

void IndexerClient::indexerConnected(const EndPoint &)
{
    m_indexConnection.send(Message(Api::IndexerService, Api::Indexer::Version));
    m_indexConnection.send(Message(Api::IndexerService, Api::Indexer::GetAvailableIndexers));
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
                else if (parser.tag() == Api::Indexer::OffsetInBlock)
                    offsetInBlock = parser.intData();
            }

            logCritical().nospace() << "Transaction location is: [block=" << blockHeight << "+" << offsetInBlock << "]";
            if (blockHeight > 0 && offsetInBlock > 80 && m_hubConnection.isValid()) {
                Streaming::MessageBuilder builder(Streaming::NoHeader, 20);
                builder.add(Api::BlockChain::BlockHeight, blockHeight);
                builder.add(Api::BlockChain::Tx_OffsetInBlock, offsetInBlock);
                m_hubConnection.send(builder.message(Api::BlockChainService, Api::BlockChain::GetTransaction));
            }
            else
                QCoreApplication::quit();
        }
        else if (message.messageId() == Api::Indexer::FindAddressReply) {
            int usageId = 0;
            Streaming::MessageParser parser(message);
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::Indexer::Separator)
                    usageId++;
            }
            const int width = digits(usageId);

            usageId = 0;
            int blockHeight = -1, offsetInBlock = 0, index = 0;
            parser = Streaming::MessageParser(message);
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::Indexer::BlockHeight)
                    blockHeight = parser.intData();
                else if (parser.tag() == Api::Indexer::OffsetInBlock)
                    offsetInBlock = parser.intData();
                else if (parser.tag() == Api::Indexer::OutIndex)
                    index = parser.intData();
                else if (parser.tag() == Api::Indexer::Separator) {
                    auto log = logCritical().nospace();
                    for (auto i = digits(++usageId); i < width; ++i)
                        log << 0;
                    log << usageId;
                    log << "] Address touches [block=" << blockHeight << "+" << offsetInBlock;
                    log << "|" << index << "]";
                    if (blockHeight > 0 && offsetInBlock > 80 && m_hubConnection.isValid()) {
                        Streaming::MessageBuilder builder(Streaming::NoHeader, 20);
                        builder.add(Api::BlockChain::BlockHeight, blockHeight);
                        builder.add(Api::BlockChain::Tx_OffsetInBlock, offsetInBlock);
                        builder.add(Api::BlockChain::Include_TxId, true);
                        Message m = builder.message(Api::BlockChainService, Api::BlockChain::GetTransaction);
                        m.setHeaderInt(Api::RequestId, usageId);
                        m_hubConnection.send(m);
                    }
                }
            }
            if (!m_hubConnection.isValid() || (blockHeight == -1))
                QCoreApplication::quit();
            m_txIdsRequested += usageId;
        }
        else if (message.messageId() == Api::Indexer::GetAvailableIndexersReply) {
            Streaming::MessageParser parser(message);
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::Indexer::AddressIndexer)
                    logInfo() << "Info: remote indexer has Address Index";
                else if (parser.tag() == Api::Indexer::TxIdIndexer)
                    logInfo() << "Info: remote indexer has TXID Index";
                else if (parser.tag() == Api::Indexer::SpentOutputIndexer)
                    logInfo() << "Info: remote indexer has SpentOutput Index";
            }
        }
        else if (message.messageId() == Api::Indexer::VersionReply) {
            Streaming::MessageParser parser(message);
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::Indexer::GenericByteData)
                    logCritical() << "Info: remote indexer at version" << parser.stringData();
            }
        } else
            Streaming::MessageParser::debugMessage(message);
    }
    else
        Streaming::MessageParser::debugMessage(message);
}
