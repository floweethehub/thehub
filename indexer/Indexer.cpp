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
#include "AddressIndexer.h"
#include "TxIndexer.h"
#include "SpentOuputIndexer.h"

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

namespace {

static std::vector<std::atomic<int> > s_requestedHeights = std::vector<std::atomic<int> >(3);

struct Token {
    Token(int wantedHeight) {
        for (int i = 0; i < s_requestedHeights.size(); ++i) {
            if (s_requestedHeights[i] == -1) {
                m_token = i;
                break;
            }
        }
        assert(m_token >= 0); // if fail, then make sure your vector size matches the max number of indexer-threads
        s_requestedHeights[m_token] = wantedHeight;
    }
    ~Token() {
        s_requestedHeights[m_token] = -1;
    }

    int allocatedTokens() const {
        int answer = 0;
        for (size_t i = 0; i < s_requestedHeights.size(); ++i) {
            if (s_requestedHeights[i].load() != -1)
                answer++;
        }
        return answer;
    }
    // assume that the allocator for the block we just requested will
    // set the token to -1 soon.
    void requestingBlock(int blockHeight) {
        for (size_t i = 0; i < s_requestedHeights.size(); ++i) {
            if (s_requestedHeights[i] == blockHeight)
                s_requestedHeights[i] = -1;
        }
    }
private:
    int m_token = -1;
};

}


Indexer::Indexer(const boost::filesystem::path &basedir)
    : NetworkService(Api::IndexerService),
      m_basedir(basedir),
    m_poolAddressAnswers(2 * 1024 * 1024),
    m_network(m_workers.ioService())
{
    qRegisterMetaType<Message>("Message");
    m_network.addService(this);

    // init static
    for (size_t i = 0; i < s_requestedHeights.size(); ++i) {
        s_requestedHeights[i] = -1;
    }

    // TODO add some auto-save of the databases.
    connect (&m_pollingTimer, SIGNAL(timeout()), SLOT(checkBlockArrived()));
    m_pollingTimer.start(2 * 60 * 1000);
    connect (this, SIGNAL(requestFindAddress(Message)), this, SLOT(onFindAddressRequest(Message)), Qt::QueuedConnection);
}

Indexer::~Indexer()
{
    if (m_txdb)
        m_txdb->requestInterruption();
    if (m_addressdb)
        m_addressdb->requestInterruption();
    if (m_spentOutputDb)
        m_spentOutputDb->requestInterruption();

    m_waitForBlock.notify_all();
    if (m_txdb) {
        m_txdb->wait();
        delete m_txdb;
    }
    if (m_addressdb) {
        m_addressdb->wait();
        delete m_addressdb;
    }
    if (m_spentOutputDb) {
        m_spentOutputDb->wait();
        delete m_spentOutputDb;
    }
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

void Indexer::loadConfig(const QString &filename, const EndPoint &prioHubLocation)
{
    using boost::asio::ip::tcp;

    if (!QFile::exists(filename))
        return;
    QSettings settings(filename, QSettings::IniFormat);
    EndPoint hub(prioHubLocation);

    bool enableTxDB = true, enableAddressDb = false, enableSpentDb = false;
    const QStringList groups = settings.childGroups();
    for (auto group : groups) {
        if (group == "addressdb") {
            enableAddressDb = settings.value("addressdb/enabled", "false").toBool();
        }
        else if (group == "txdb") {
            enableTxDB = settings.value("txdb/enabled", "false").toBool();
        }
        else if (group == "spentdb") {
            enableSpentDb = settings.value("spentdb/enabled", "false").toBool();
        }
        else if (group == "services") {
            if (!hub.isValid()) { // only if user didn't override using commandline
                QString connectionString = settings.value("services/hub").toString();
                hub = EndPoint("", 1234);
                SplitHostPort(connectionString.toStdString(), hub.announcePort, hub.hostname);
            }
        }
        else if (settings.value(group + "/ip").isValid()) {
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
        else {
            logCritical().nospace() << "Config file has unrecognized or empty group. Skipping: "
                 << "[" << group << "]";
        }
    }

    if (!m_isServer) // then add localhost
        bind(tcp::endpoint(boost::asio::ip::address_v4::loopback(), 1234));
    if (!m_isServer) // then add localhost ipv6
        bind(tcp::endpoint(boost::asio::ip::address_v6::loopback(), 1234));

    // make sure we have the right workers.
    QList<QThread*> newThreads;
    if (enableAddressDb && !m_addressdb) {
        m_addressdb = new AddressIndexer(m_basedir / "addresses", this);
        newThreads.append(m_addressdb);
    } else if (!enableAddressDb && m_addressdb) {
        m_addressdb->requestInterruption();
        m_addressdb->wait();
        delete m_addressdb;
        m_addressdb = nullptr;
    }
    if (enableTxDB && !m_txdb) {
        m_txdb = new TxIndexer(m_workers.ioService(), m_basedir / "txindex", this);
        newThreads.append(m_txdb);
    } else if (!enableTxDB && m_txdb) {
        m_txdb->requestInterruption();
        m_txdb->wait();
        delete m_txdb;
        m_txdb = nullptr;
    }
    if (enableSpentDb && !m_spentOutputDb) {
        m_spentOutputDb = new SpentOutputIndexer(m_workers.ioService(), m_basedir / "spent", this);
        newThreads.append(m_spentOutputDb);
    } else if (!enableSpentDb && m_spentOutputDb) {
        m_spentOutputDb->requestInterruption();
        m_spentOutputDb->wait();
        delete m_spentOutputDb;
        m_spentOutputDb = nullptr;
    }

    // connect to upstream Hub
    try {
        if (hub.isValid())
            tryConnectHub(hub);
    } catch (const std::exception &e) {
        logFatal() << "Config: Hub connection string invalid." << e;
    }

    // start new threads as the last thing we do
    for (auto t : newThreads) {
        t->start();
    }
}

void Indexer::onIncomingMessage(NetworkService::Remote *con, const Message &message, const EndPoint &)
{
    Q_ASSERT(message.serviceId() == Api::IndexerService);
    if (message.messageId() == Api::Indexer::GetAvailableIndexers) {
        con->pool.reserve(10);
        Streaming::MessageBuilder builder(con->pool);
        if (m_txdb)
            builder.add(Api::Indexer::TxIdIndexer, true);
        if (m_addressdb)
            builder.add(Api::Indexer::AddressIndexer, true);
        if (m_spentOutputDb)
            builder.add(Api::Indexer::SpentOutputIndexer, true);
        con->connection.send(builder.message(Api::IndexerService,
                                             Api::Indexer::GetAvailableIndexersReply));
    }
    else if (message.messageId() == Api::Indexer::FindTransaction) {
        if (!m_txdb) {
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
                auto data = m_txdb->find(*txid);
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
        if (!m_addressdb) {
            con->connection.disconnect();
            return;
        }

        // since the AddressDB is backed by a slow SQL database, move the
        // handlign out of this thread in order to keep networkmanager IO going fast.
        emit requestFindAddress(message);
    }
    else if (message.messageId() == Api::Indexer::FindSpentOutput) {
        if (!m_spentOutputDb) {
            con->connection.disconnect();
            return;
        }

        Streaming::MessageParser parser(message.body());
        const uint256 *txid = nullptr;
        int outIndex = 0;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::Indexer::TxId) {
                if (parser.dataLength() != 32) {
                    con->connection.disconnect();
                    return;
                }
                txid = reinterpret_cast<const uint256*>(parser.bytesDataBuffer().begin());
            }
            else if (parser.tag() == Api::Indexer::OutIndex) {
                outIndex = parser.intData();
            }
        }

        if (txid == nullptr || outIndex < 0) {
            con->connection.disconnect();
            return;
        }
        auto data = m_spentOutputDb->findSpendingTx(*txid, outIndex);
        con->pool.reserve(20);
        Streaming::MessageBuilder builder(con->pool);
        builder.add(Api::Indexer::BlockHeight, data.blockHeight);
        builder.add(Api::Indexer::OffsetInBlock, data.offsetInBlock);
        Message reply = builder.message(Api::IndexerService, Api::Indexer::FindSpentOutputReply);
        const int requestId = message.headerInt(Api::RequestId);
        if (requestId != -1)
            reply.setHeaderInt(Api::RequestId, requestId);
        con->connection.send(reply);
    }
}

Message Indexer::nextBlock(int height, unsigned long timeout)
{
    logDebug() << height;
    QMutexLocker lock(&m_nextBlockLock);
    // store an RAII token to synchronize all threads.
    Token token(height);
    while (!QThread::currentThread()->isInterruptionRequested()) {
        if (m_nextBlock.serviceId() == Api::BlockChainService && m_nextBlock.messageId() == Api::BlockChain::GetBlockReply) {
            Streaming::MessageParser parser(m_nextBlock.body());
            parser.next();
            if (parser.tag() == Api::BlockChain::BlockHeight && parser.intData() == height)
                return m_nextBlock;
        }

        int totalWanted = 0;
        if (m_txdb) totalWanted++;
        if (m_spentOutputDb) totalWanted++;
        if (m_addressdb) totalWanted++;
        if (token.allocatedTokens() == totalWanted) {
            requestBlock();
            // make the token de-allocate all threads we expect to get served
            // to avoid us re-requesting this same block again in race-conditions.
            token.requestingBlock(m_lastRequestedBlock);
        }

        // wait until the network-manager thread actually finds the block-message as sent by the Hub
        m_waitForBlock.wait(&m_nextBlockLock, timeout);
    }
    return Message();
}

void Indexer::checkBlockArrived()
{
    if (!m_serverConnection.isConnected())
        return;
    QMutexLocker lock(&m_nextBlockLock);
    if (m_lastRequestedBlock != 0 && QDateTime::currentMSecsSinceEpoch() - m_timeLastRequest > 20000) {
        logDebug() << "repeating block request";
        // Hub never sent the block to us :(
        m_lastRequestedBlock = 0;
        requestBlock();
    }
}

void Indexer::onFindAddressRequest(const Message &message)
{
    NetworkConnection con;
    try {
        con = std::move(m_network.connection(m_network.endPoint(message.remote), NetworkManager::OnlyExisting));
    } catch (...) {
        // remote no longer connected.
        return;
    }
    if (!con.isConnected())
        return;

    Streaming::MessageParser parser(message);
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::Indexer::BitcoinAddress) {
            if (parser.dataLength() != 20) {
                con.disconnect();
                return;
            }
            const uint160 *a = reinterpret_cast<const uint160*>(parser.bytesDataBuffer().begin());
            logDebug() << "FindAddress on address:" << *a;
            auto data = m_addressdb->find(*a);
            m_poolAddressAnswers.reserve(data.size() * 30);
            Streaming::MessageBuilder builder(m_poolAddressAnswers);
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
            con.send(reply);
            return; // just one request per message
        }
    }
}

void Indexer::hubConnected(const EndPoint &ep)
{
    int txHeight = m_txdb ? m_txdb->blockheight() : -1;
    int adHeight = m_addressdb ? m_addressdb->blockheight() : -1;
    int spentHeight = m_spentOutputDb ? m_spentOutputDb->blockheight() : -1;
    logCritical() << "Connection to hub established." << ep << "TxDB:" << txHeight
                  << "addressDB:" << adHeight
                  << "spentOutputDB" << spentHeight;
    m_serverConnection.send(Message(Api::APIService, Api::Meta::Version));
    m_serverConnection.send(Message(Api::BlockNotificationService, Api::BlockNotification::Subscribe));
    QMutexLocker lock(&m_nextBlockLock);
    requestBlock();
}

void Indexer::requestBlock()
{
    int blockHeight = 9999999;
    for (size_t i = 0; i < s_requestedHeights.size(); ++i) {
        int h = s_requestedHeights.at(i).load();
        if (h != -1)
            blockHeight = std::min(h, blockHeight);
    }
    if (blockHeight == 9999999)
        return;
    assert(m_lastRequestedBlock != blockHeight);
    m_lastRequestedBlock = blockHeight;
    m_timeLastRequest = QDateTime::currentMSecsSinceEpoch();
    m_pool.reserve(20);
    Streaming::MessageBuilder builder(m_pool);
    builder.add(Api::BlockChain::BlockHeight, blockHeight);
    if (m_txdb)
        builder.add(Api::BlockChain::Include_TxId, true);
    if (m_addressdb)
        builder.add(Api::BlockChain::Include_OutputAddresses, true);
    if (m_spentOutputDb)
        builder.add(Api::BlockChain::Include_Inputs, true);
    builder.add(Api::BlockChain::Include_OffsetInBlock, true);
    logDebug() << "requesting block" << blockHeight;
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
            int blockHeight = -1;
            Streaming::MessageParser parser(message.body());
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::BlockChain::BlockHeight) {
                    blockHeight = parser.intData();
                    logDebug() << "Hub sent us block" << blockHeight;
                    if (blockHeight % 500 == 0)
                        logCritical() << "Processing block" << blockHeight;
                    break;
                }
            }
            QMutexLocker lock(&m_nextBlockLock);
            if (m_lastRequestedBlock == blockHeight) {
                m_nextBlock = message;
                m_lastRequestedBlock = 0;
                m_waitForBlock.notify_all();
            }
        }
    }
    else if (message.serviceId() == Api::APIService) {
        if (message.messageId() == Api::Meta::VersionReply) {
            Streaming::MessageParser parser(message.body());
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::Meta::GenericByteData) {
                    logInfo() << "Server is at version" << parser.stringData();
                }
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
                if (m_addressdb)
                    logCritical() << "AddressDB now at:" << m_addressdb->blockheight();
                if (m_txdb)
                    logCritical() << "txDb now at:" << m_txdb->blockheight();
                if (m_spentOutputDb)
                    logCritical() << "spentDB now at:" << m_spentOutputDb->blockheight();
                m_indexingFinished = true;
                m_lastRequestedBlock = 0;
                if (m_addressdb)
                    m_addressdb->flush();
                if (m_txdb)
                    m_txdb->saveCaches();
                if (m_spentOutputDb)
                    m_spentOutputDb->saveCaches();
            }
            else logCritical() << "Failure detected" << serviceId << messageId;
        }
    }
    else if (message.serviceId() == Api::BlockNotificationService && message.messageId() == Api::BlockNotification::NewBlockOnChain) {
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockNotification::BlockHeight) {
                if (!m_spentOutputDb && !m_addressdb && !m_txdb) return; // user likes to torture us. :(
                int blockHeight = 9999999;
                if (m_addressdb)
                    blockHeight = m_addressdb->blockheight();
                if (m_txdb)
                    blockHeight = std::min(blockHeight, m_txdb->blockheight());
                if (m_spentOutputDb)
                    blockHeight = std::min(blockHeight, m_spentOutputDb->blockheight());
                if (parser.intData() == blockHeight + 1
                        || (m_indexingFinished && parser.intData() >= blockHeight)) {
                    m_indexingFinished = false;
                    m_lastRequestedBlock = 0;
                    QMutexLocker lock(&m_nextBlockLock);
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
