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
#include <qsettings.h>
#include <primitives/pubkey.h>

#include <qfile.h>
#include <qcoreapplication.h>

Indexer::Indexer(const boost::filesystem::path &basedir)
    : NetworkService(Api::IndexerService),
    m_txdb(m_workers.ioService(), basedir / "txindex"),
    m_addressdb(basedir / "addresses"),
    m_network(m_workers.ioService())
{
    m_network.addService(this);

    // TODO add some auto-save of the databases.
    connect (&m_addressdb, SIGNAL(finishedProcessingBlock()), SLOT(addressDbFinishedProcessingBlock()));
    connect (&m_pollingTimer, SIGNAL(timeout()), SLOT(poll4Block()));
    m_pollingTimer.start(2 * 60 * 1000);
}

Indexer::~Indexer()
{
    m_serverConnection.clear(NetworkConnection::ClearAndWait);
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

void Indexer::bind(boost::asio::ip::tcp::endpoint endpoint)
{
    m_network.bind(endpoint, std::bind(&Indexer::clientConnected, this, std::placeholders::_1));
}

void Indexer::loadConfig(const QString &filename)
{
    if (!QFile::exists(filename))
        return;
    QSettings settings(filename, QSettings::IniFormat);

    const QStringList groups = settings.childGroups();
    if (groups.contains("addressdb")) {
        m_enableAddressDb = settings.value("addressdb/enabled", "true").toBool();
        if (m_enableAddressDb)
            m_addressdb.loadSetting(settings);
    }
    if (groups.contains("txdb")) {
        m_enableTxDB = settings.value("txdb/enabled", "true").toBool();
        // no config there yet
    }
}

void Indexer::onIncomingMessage(NetworkService::Remote *con, const Message &message, const EndPoint &)
{
    Q_ASSERT(message.serviceId() == Api::IndexerService);
    if (message.messageId() == Api::Indexer::GetAvailableIndexers) {
        con->pool.reserve(10);
        Streaming::MessageBuilder builder(con->pool);
        if (m_enableTxDB)
            builder.add(Api::Indexer::TxIdIndexer, true);
        if (m_enableAddressDb)
            builder.add(Api::Indexer::AddressIndexer, true);
        con->connection.send(builder.message(Api::IndexerService,
                                             Api::Indexer::GetAvailableIndexersReply));
    }
    else if (message.messageId() == Api::Indexer::FindTransaction) {
        if (!m_enableTxDB) {
            con->connection.disconnect();
            return;
        }

        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::Indexer::TxId) {
                if (parser.dataLength() != 32) {
                    con->connection.disconnect();
                    return;
                }
                const uint256 *txid = reinterpret_cast<const uint256*>(parser.bytesDataBuffer().begin());
                auto data = m_txdb.find(*txid);
                con->pool.reserve(20);
                Streaming::MessageBuilder builder(con->pool);
                builder.add(Api::Indexer::BlockHeight, data.blockHeight);
                builder.add(Api::Indexer::OffsetInBlock, data.offsetInBlock);
                Message m = builder.message(Api::IndexerService, Api::Indexer::FindTransactionReply);
                int requestId = message.headerInt(Api::RequestId);
                if (requestId != -1)
                    m.setHeaderInt(Api::RequestId, requestId);
                con->connection.send(m);
                return; // just one item per message
            }
        }
    }
    else if (message.messageId() == Api::Indexer::FindAddress) {
        if (!m_enableAddressDb) {
            con->connection.disconnect();
            return;
        }

        Streaming::MessageParser parser(message);
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::Indexer::BitcoinAddress) {
                if (parser.dataLength() != 20) {
                    con->connection.disconnect();
                    return;
                }
                const uint160 *a = reinterpret_cast<const uint160*>(parser.bytesDataBuffer().begin());
                auto data = m_addressdb.find(*a);
                con->pool.reserve(data.size() * 25);
                Streaming::MessageBuilder builder(con->pool);
                for (auto item : data) {
                    builder.add(Api::Indexer::BlockHeight, item.blockHeight);
                    builder.add(Api::Indexer::OffsetInBlock, item.offsetInBlock);
                    builder.add(Api::Indexer::OutIndex, item.outputIndex);
                }

                Message m = builder.message(Api::IndexerService, Api::Indexer::FindAddressReply);
                int requestId = message.headerInt(Api::RequestId);
                if (requestId != -1)
                    m.setHeaderInt(Api::RequestId, requestId);
                con->connection.send(m);
                return; // just one request per message
            }
        }
    }
}

void Indexer::addressDbFinishedProcessingBlock()
{
    requestBlock();
}

void Indexer::poll4Block()
{
    if (m_indexingFinished)
        requestBlock();
}

void Indexer::hubConnected(const EndPoint &ep)
{
    int txHeight = m_enableTxDB ? m_txdb.blockheight() : -1;
    int adHeight = m_enableAddressDb ? m_addressdb.blockheight() : -1;
    logCritical() << "Connection to hub established. TxDB:" << txHeight
                  << "addressDB:" << adHeight;
    m_serverConnection.send(Message(Api::APIService, Api::Meta::Version));
    m_serverConnection.send(Message(Api::BlockNotificationService, Api::BlockNotification::Subscribe));
    if (!m_addressdb.isCommitting())
        requestBlock();
}

void Indexer::requestBlock()
{
    if (!m_enableAddressDb && !m_enableTxDB)
        return;
    int blockHeight = 9999999;
    if (m_enableAddressDb)
        blockHeight = m_addressdb.blockheight();
    if (m_enableTxDB)
        blockHeight = std::min(blockHeight, m_txdb.blockheight());
    blockHeight++; // request the next one
    m_pool.reserve(20);
    Streaming::MessageBuilder builder(m_pool);
    builder.add(Api::BlockChain::BlockHeight, blockHeight);
    if (m_enableTxDB && m_txdb.blockheight() < blockHeight)
        builder.add(Api::BlockChain::Include_TxId, true);
    if (m_enableAddressDb && m_addressdb.blockheight() < blockHeight)
        builder.add(Api::BlockChain::Include_OutputAddresses, true);
    builder.add(Api::BlockChain::Include_OffsetInBlock, true);
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
            processNewBlock(message);
        }
    }
    else if (message.serviceId() == Api::APIService) {
        if (message.messageId() == Api::Meta::VersionReply) {
            Streaming::MessageParser parser(message.body());
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::Meta::GenericByteData)
                    logInfo() << "Server is at version" << parser.stringData();
            }
        }
        else if (message.messageId() == Api::Meta::CommandFailed) {
            Streaming::MessageParser parser(message.body());
            int serviceId = -1;
            int messageId = -1;
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::Meta::FailedCommandServiceId)
                    serviceId = parser.intData();
                else if (parser.tag() == Api::Meta::FailedCommandId)
                    messageId = parser.intData();
            }
            if (serviceId == Api::BlockChainService && messageId == Api::BlockChain::GetBlock) {
                logCritical() << "Failed to get block, assuming we are at 'top' of chain";
                m_indexingFinished = true;
                m_addressdb.flush();
            }
        }
    }
    else if (message.serviceId() == Api::BlockNotificationService && message.messageId() == Api::BlockNotification::NewBlockOnChain) {
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockNotification::BlockHeight) {
                if (!m_enableAddressDb && !m_enableTxDB) return; // user likes to torture us. :(
                int blockHeight = 9999999;
                if (m_enableAddressDb)
                    blockHeight = m_addressdb.blockheight();
                if (m_enableTxDB)
                    blockHeight = std::min(blockHeight, m_txdb.blockheight());
                if (parser.intData() == blockHeight + 1)
                    requestBlock();
            }
        }
    }
    else {
        Streaming::MessageParser::debugMessage(message);
    }
}

void Indexer::clientConnected(NetworkConnection &con)
{
    logCritical() << "A client connected";
    con.accept();
}

void Indexer::processNewBlock(const Message &message)
{
    m_indexingFinished = false;
    int txOffsetInBlock = 0;
    int outputIndex = -1;
    uint256 blockId;
    uint256 txid;
    Streaming::MessageParser parser(message.body());
    int blockHeight = -1;

    bool updateTxDb = false;
    bool updateAddressDb = false;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::BlockChain::BlockHeight) {
            if (blockHeight != -1) Streaming::MessageParser::debugMessage(message);
            assert(blockHeight == -1);
            blockHeight = parser.intData();
            updateTxDb = m_enableTxDB && blockHeight == m_txdb.blockheight() + 1;
            updateAddressDb = m_enableAddressDb && blockHeight == m_addressdb.blockheight() + 1;
            if (blockHeight % 500 == 0)
                logCritical() << "Processing block" << blockHeight;
        } else if (parser.tag() == Api::BlockChain::BlockHash) {
            blockId = parser.uint256Data();
        } else if (parser.tag() == Api::BlockChain::Separator) {
            if (updateTxDb && txOffsetInBlock > 0 && !txid.IsNull()) {
                assert(blockHeight > 0);
                assert(blockHeight > m_txdb.blockheight());
                try {
                    m_txdb.insert(txid, blockHeight, txOffsetInBlock);
                } catch(...) {
                    m_enableTxDB = false;
                }
            }
            txOffsetInBlock = 0;
            outputIndex = -1;
        } else if (parser.tag() == Api::BlockChain::Tx_OffsetInBlock) {
            txOffsetInBlock = parser.intData();
        } else if (parser.tag() == Api::BlockChain::TxId) {
            txid = parser.uint256Data();
        } else if (parser.tag() == Api::BlockChain::Tx_Out_Index) {
            outputIndex = parser.intData();
        } else if (updateAddressDb && parser.tag() == Api::BlockChain::Tx_Out_Address) {
            assert(parser.dataLength() == 20);
            assert(outputIndex >= 0);
            assert(blockHeight > 0);
            assert(txOffsetInBlock > 0);
            try {
                m_addressdb.insert(parser.bytesDataBuffer(), outputIndex, blockHeight, txOffsetInBlock);
            } catch(...) {
                m_enableAddressDb = false;
            }
        }
    }
    assert(blockHeight > 0);
    assert(!blockId.IsNull());
    if (updateTxDb) try {
        m_txdb.blockFinished(blockHeight, blockId);
    } catch(...) {
        m_enableTxDB = false;
    }
    if (updateAddressDb) try {
        m_addressdb.blockFinished(blockHeight, blockId);
    } catch(...) {
        m_enableAddressDb = false;
    }
    // if not updating addressDB, go to next block directly instead of waiting on its signal
    if (!updateAddressDb && updateTxDb)
        requestBlock();
}
