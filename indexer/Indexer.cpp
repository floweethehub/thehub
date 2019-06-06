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
#include <qdatetime.h>
#include <primitives/pubkey.h>
#include <netbase.h>

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
    connect (&m_pollingTimer, SIGNAL(timeout()), SLOT(checkBlockArrived()));
    m_pollingTimer.start(2 * 60 * 1000);
}

Indexer::~Indexer()
{
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
    m_isServer = true;
}

void Indexer::loadConfig(const QString &filename)
{
    using boost::asio::ip::tcp;

    if (!QFile::exists(filename))
        return;
    QSettings settings(filename, QSettings::IniFormat);

    const QStringList groups = settings.childGroups();
    for (auto group : groups) {
        if (group == "addressdb") {
            m_enableAddressDb = settings.value("addressdb/enabled", "true").toBool();
            if (m_enableAddressDb)
                m_addressdb.loadSetting(settings);
        }
        else if (group == "txdb") {
            m_enableTxDB = settings.value("txdb/enabled", "true").toBool();
            // no config there yet
        }
        else if (group == "services") {
            if (!m_serverConnection.isValid()) { // only if user didn't override using commandline
                QString connectionString = settings.value("services/hub").toString();
                EndPoint ep("", 1234);
                SplitHostPort(connectionString.toStdString(), ep.announcePort, ep.hostname);
                try {
                    tryConnectHub(ep);
                } catch (const std::exception &e) {
                    logFatal() << "Config: Hub connection string invalid.";
                }
            }
        }
        else {
            EndPoint ep("", 1234);
            auto portVar = settings.value(group + "/port");
            if (portVar.isValid()) {
                bool ok;
                ep.announcePort = portVar.toInt(&ok);
                if (!ok) {
                    logCritical() << "Config file has 'port' value that is not a number.";
                    continue;
                }
            }
            try {
                QString bindAddress = settings.value(group + "/ip").toString();
                ep.ipAddress = bindAddress == "localhost"
                        ? boost::asio::ip::address_v4::loopback()
                        : boost::asio::ip::address::from_string(bindAddress.toStdString());
            } catch (const std::runtime_error &e) {
                logCritical() << "Config file has invalid IP address value to bind to.";
                continue;
            }
            logCritical().nospace() << "Binding to " << ep.ipAddress.to_string().c_str() << ":" << ep.announcePort;
            try {
                bind(tcp::endpoint(ep.ipAddress, ep.announcePort));
            } catch (std::exception &e) {
                logCritical() << "  " << e << "skipping";
            }
        }
    }

    if (!m_isServer) // then add localhost
        bind(tcp::endpoint(boost::asio::ip::address_v4::loopback(), 1234));
    if (!m_isServer) // then add localhost ipv6
        bind(tcp::endpoint(boost::asio::ip::address_v6::loopback(), 1234));
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
                Message reply = builder.message(Api::IndexerService, Api::Indexer::FindTransactionReply);
                const int requestId = message.headerInt(Api::RequestId);
                if (requestId != -1)
                    reply.setHeaderInt(Api::RequestId, requestId);
                con->connection.send(reply);
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
                logDebug() << "FindAddress on address:" << *a;
                auto data = m_addressdb.find(*a);
                con->pool.reserve(data.size() * 25);
                Streaming::MessageBuilder builder(con->pool);
                int bh = -1, oib = -1;
                for (auto item : data) {
                    if (item.blockHeight != bh) // avoid repeating oneself (makes the message smaller).
                        builder.add(Api::Indexer::BlockHeight, item.blockHeight);
                    bh = item.blockHeight;
                    if (item.offsetInBlock != oib)
                        builder.add(Api::Indexer::OffsetInBlock, item.offsetInBlock);
                    oib = item.offsetInBlock;
                    builder.add(Api::Indexer::OutIndex, item.outputIndex);
                    builder.add(Api::Indexer::Separator, true);
                }

                Message reply = builder.message(Api::IndexerService, Api::Indexer::FindAddressReply);
                const int requestId = message.headerInt(Api::RequestId);
                if (requestId != -1)
                    reply.setHeaderInt(Api::RequestId, requestId);
                con->connection.send(reply);
                return; // just one request per message
            }
        }
    }
}

void Indexer::addressDbFinishedProcessingBlock()
{
    requestBlock();
}

void Indexer::checkBlockArrived()
{
    if (!m_serverConnection.isConnected())
        return;
    if (m_lastRequestedBlock != 0 && QDateTime::currentMSecsSinceEpoch() - m_timeLastRequest > 20000) {
        logDebug() << "repeating block request";
        // Hub never sent the block to us :(
        m_lastRequestedBlock = 0;
        requestBlock();
    }
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
    if (m_lastRequestedBlock == blockHeight)
        return;
    m_lastRequestedBlock = blockHeight;
    m_timeLastRequest = QDateTime::currentMSecsSinceEpoch();
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
            try {
                processNewBlock(message);
            } catch (const std::exception &e) {
                logFatal() << "processing block failed due to:" << e;
                QCoreApplication::exit(4);
            }
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
                else if (parser.tag() == Api::Meta::FailedReason)
                    logDebug() << "failed reason:" << parser.stringData();
            }
            if (serviceId == Api::BlockChainService && messageId == Api::BlockChain::GetBlock) {
                logCritical() << "Failed to get block, assuming we are at 'top' of chain";
                if (m_enableAddressDb)
                    logCritical() << "AddressDB now at:" << m_addressdb.blockheight();
                if (m_enableTxDB)
                    logCritical() << "txDb now at:" << m_txdb.blockheight();
                m_indexingFinished = true;
                m_lastRequestedBlock = 0;
                m_addressdb.flush();
                m_txdb.saveCaches();
            }
            else logCritical() << "Failure detected" << serviceId << messageId;
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
                if (m_indexingFinished || parser.intData() == blockHeight + 1) {
                    m_indexingFinished = false;
                    m_lastRequestedBlock = 0;
                    requestBlock();
                }
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
            if (m_lastRequestedBlock <= blockHeight)
                m_lastRequestedBlock= 0;
        } else if (parser.tag() == Api::BlockChain::BlockHash) {
            blockId = parser.uint256Data();
        } else if (parser.tag() == Api::BlockChain::Separator) {
            if (updateTxDb && txOffsetInBlock > 0 && !txid.IsNull()) {
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
        } else if (updateAddressDb && parser.tag() == Api::BlockChain::Tx_Out_Address) {
            assert(parser.dataLength() == 20);
            assert(outputIndex >= 0);
            assert(blockHeight > 0);
            assert(txOffsetInBlock > 0);
            m_addressdb.insert(parser.bytesDataBuffer(), outputIndex, blockHeight, txOffsetInBlock);
        }
    }
    assert(blockHeight > 0);
    assert(!blockId.IsNull());
    if (m_enableTxDB)
        m_txdb.blockFinished(blockHeight, blockId);
    if (m_enableAddressDb)
        m_addressdb.blockFinished(blockHeight, blockId);
    // if not updating addressDB, go to next block directly instead of waiting on its signal
    if (!updateAddressDb && updateTxDb)
        requestBlock();
}
