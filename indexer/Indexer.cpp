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
#include <cashaddr.h>
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>
#include <APIProtocol.h>
#include <qbytearray.h>
#include <qsettings.h>
#include <qdatetime.h>
#include <primitives/pubkey.h>
#include <utilstrencodings.h>

#include <qfile.h>
#include <qcoreapplication.h>

namespace {

static std::vector<std::atomic<int> > s_requestedHeights = std::vector<std::atomic<int> >(3);

struct Token {
    Token(int wantedHeight) : m_wantedHeight(wantedHeight) {
        for (int i = 0; i < s_requestedHeights.size(); ++i) {
            int expected = -1;
            if (s_requestedHeights[i].compare_exchange_strong(expected, wantedHeight)) {
                m_token = i;
                break;
            }
        }
        assert(m_token >= 0); // if fail, then make sure your vector size matches the max number of indexer-threads
    }
    ~Token() {
        // only exchange when someone else didn't take our slot yet.
        s_requestedHeights[m_token].compare_exchange_strong(m_wantedHeight, -1);
    }

    int allocatedTokens() const {
        int answer = 0;
        for (size_t i = 0; i < s_requestedHeights.size(); ++i) {
            if (s_requestedHeights[i].load() != -1)
                answer++;
        }
        return answer;
    }
private:
    int m_token = -1;
    int m_wantedHeight = 0;
};

void buildAddressSearchReply(Streaming::MessageBuilder &builder, const std::vector<AddressIndexer::TxData> &data)
{
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
}
}


Indexer::Indexer(const boost::filesystem::path &basedir)
    : NetworkService(Api::IndexerService),
    m_poolAddressAnswers(2 * 1024 * 1024),
    m_basedir(basedir),
    m_network(m_workers.ioService()),
    m_bestBlockHeight(0)
{
    qRegisterMetaType<Message>("Message");
    m_network.addService(this);

    // init static
    for (size_t i = 0; i < s_requestedHeights.size(); ++i) {
        s_requestedHeights[i] = -1;
    }

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

    m_waitForBlock.wakeAll();
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
    m_serverConnection = m_network.connection(ep);
    if (!m_serverConnection.isValid())
        throw std::runtime_error("Invalid Endpoint, can't create connection");
    m_serverConnection.setOnConnected(std::bind(&Indexer::hubConnected, this, std::placeholders::_1));
    m_serverConnection.setOnDisconnected(std::bind(&Indexer::hubDisconnected, this));
    m_serverConnection.setOnIncomingMessage(std::bind(&Indexer::hubSentMessage, this, std::placeholders::_1));
    m_serverConnection.connect();
}

void Indexer::bind(const boost::asio::ip::tcp::endpoint &endpoint)
{
    m_network.bind(endpoint);
    m_isServer = true;
}

void Indexer::loadConfig(const QString &filename, const EndPoint &prioHubLocation)
{
    using boost::asio::ip::tcp;
    EndPoint hub(prioHubLocation);

    if (!QFile::exists(filename)) {
        if (m_txdb == nullptr && hub.isValid()) {
            // lets do SOMETHING by default.
            m_txdb = new TxIndexer(m_workers.ioService(), m_basedir / "txindex", this);
            tryConnectHub(hub);
            QTimer::singleShot(500, m_txdb, SLOT(start()));
        }
        return;
    }
    QSettings settings(filename, QSettings::IniFormat);

    bool enableTxDB = false, enableAddressDb = false, enableSpentDb = false;
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
            if (hub.hostname.empty()) { // only if user didn't override using commandline
                QString connectionString = settings.value("services/hub").toString();
                hub = EndPoint("", 1235); // clear the IP address-default
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
            } catch (const std::runtime_error &) {
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
        m_addressdb->loadSetting(settings);
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

    // start new threads as the last thing we do, put it in the future for stability.
    for (auto t : newThreads) {
        QTimer::singleShot(500, t, SLOT(start()));
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
        con->connection.send(builder.reply(message));
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
                con->connection.send(builder.reply(message));
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
        // handling out of this thread in order to keep networkmanager IO going fast.
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
        con->connection.send(builder.reply(message));
    }
}

Message Indexer::nextBlock(int height, int *knownTip, unsigned long timeout)
{
    if (knownTip)
        *knownTip = 0;
    QMutexLocker lock(&m_nextBlockLock);
    // store an RAII token to synchronize all threads.
    Token token(height);
    while (!QThread::currentThread()->isInterruptionRequested()) {
        if (m_nextBlock.serviceId() == Api::BlockChainService && m_nextBlock.messageId() == Api::BlockChain::GetBlockReply) {
            Streaming::MessageParser parser(m_nextBlock.body());
            parser.next();
            if (parser.tag() == Api::BlockChain::BlockHeight && parser.intData() == height) {
                if (knownTip)
                    *knownTip = m_bestBlockHeight.load();
                return m_nextBlock;
            }
        }

        int totalWanted = 0;
        if (m_txdb) totalWanted++;
        if (m_spentOutputDb) totalWanted++;
        if (m_addressdb) totalWanted++;
        if (token.allocatedTokens() == totalWanted) {
            if (height <= m_bestBlockHeight.load())
                requestBlock();
            else
                logInfo() << "Reached top of chain" << m_bestBlockHeight.load();
        }

        // wait until the network-manager thread actually finds the block-message as sent by the Hub
        if (!m_waitForBlock.wait(&m_nextBlockLock, timeout))
            break;
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
        requestBlock(m_lastRequestedBlock);
    }

    // also poll the block count, so we can progress if for some reason the notification was not send.
    m_serverConnection.send(Message(Api::BlockChainService, Api::BlockChain::GetBlockCount));
}

void Indexer::onFindAddressRequest(const Message &message)
{
    NetworkConnection con;
    try {
        con = m_network.connection(m_network.endPoint(message.remote), NetworkManager::OnlyExisting);
    } catch (...) {
        // remote no longer connected.
        return;
    }
    if (!con.isConnected())
        return;

    Streaming::MessageParser parser(message);
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::Indexer::BitcoinScriptHashed) {
            if (parser.dataLength() != 32) {
                con.disconnect();
                return;
            }
            const uint256 *a = reinterpret_cast<const uint256*>(parser.bytesDataBuffer().begin());
            logDebug() << "FindAddress on hash:" << *a;
            auto data = m_addressdb->find(*a);

            m_poolAddressAnswers.reserve(data.size() * 30);
            Streaming::MessageBuilder builder(m_poolAddressAnswers);
            buildAddressSearchReply(builder, data);
            con.send(builder.reply(message));
            return; // just one request per message
        }
        if (parser.tag() == Api::Indexer::BitcoinP2PKHAddress) {
            if (parser.dataLength() != 20) {
                con.disconnect();
                return;
            }
            logDebug() << "FindAddress on address" << parser.bytesDataBuffer();
            static const uint8_t prefix[3] = { 0x76, 0xA9, 20}; // OP_DUP OP_HASH160, 20-byte-push
            static const uint8_t postfix[2] = { 0x88, 0xAC }; // OP_EQUALVERIFY OP_CHECKSIG
            CSHA256 sha;
            sha.Write(prefix, 3);
            sha.Write(reinterpret_cast<const uint8_t*>(parser.bytesDataBuffer().begin()), 20);
            sha.Write(postfix, 2);
            uint256 hash;
            sha.Finalize(reinterpret_cast<unsigned char*>(&hash));
            logDebug() << "          + on hash:" << hash;
            auto data = m_addressdb->find(hash);
            m_poolAddressAnswers.reserve(data.size() * 30);
            Streaming::MessageBuilder builder(m_poolAddressAnswers);
            buildAddressSearchReply(builder, data);
            con.send(builder.reply(message));
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
    m_serverConnection.send(Message(Api::BlockChainService, Api::BlockChain::GetBlockCount));
    m_serverConnection.send(Message(Api::BlockNotificationService, Api::BlockNotification::Subscribe));
    QMutexLocker lock(&m_nextBlockLock);
    requestBlock(m_lastRequestedBlock);
}

void Indexer::requestBlock(int newBlockHeight)
{
    if (!m_serverConnection.isConnected()) {
        logCritical() << "Waiting for hub" << m_serverConnection.endPoint();
        return;
    }

    int blockHeight = 9999999;
    for (size_t i = 0; i < s_requestedHeights.size(); ++i) {
        int h = s_requestedHeights.at(i).load();
        if (h != -1)
            blockHeight = std::min(h, blockHeight);
    }
    if (blockHeight == 9999999) {
        if (newBlockHeight == m_lastRequestedBlock && m_lastRequestedBlock > 0)
            // we restart or timeout and someone requests the m_lastRequested one again.
            blockHeight = newBlockHeight;
        else // no valid block to get.
            return;
    }

    // Unset requests now we acted on them.
    for (size_t i = 0; i < s_requestedHeights.size(); ++i) {
        int expected = blockHeight;
        s_requestedHeights[i].compare_exchange_strong(expected, -1);
    }
    m_lastRequestedBlock = blockHeight;
    m_timeLastRequest = QDateTime::currentMSecsSinceEpoch();
    m_pool.reserve(20);
    Streaming::MessageBuilder builder(m_pool);
    builder.add(Api::BlockChain::BlockHeight, blockHeight);
    if (m_txdb)
        builder.add(Api::BlockChain::Include_TxId, true);
    if (m_addressdb)
        builder.add(Api::BlockChain::Include_OutputScriptHash, true);
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
                    if ((blockHeight % 500) == 0 || m_timeLastLogLine + 2000 < QDateTime::currentMSecsSinceEpoch()) {
                        m_timeLastLogLine = QDateTime::currentMSecsSinceEpoch();
                        logCritical() << "Processing block" << blockHeight;
                    }
                    break;
                }
            }
            QMutexLocker lock(&m_nextBlockLock);
            if (m_lastRequestedBlock == blockHeight) {
                m_nextBlock = message;
                m_lastRequestedBlock = 0;
                m_waitForBlock.wakeAll();
            }
        }
        else if (message.messageId() == Api::BlockChain::GetBlockCountReply) {
            Streaming::MessageParser parser(message.body());
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::BlockChain::BlockHeight) {
                    const int tipHeight = parser.intData();
                    if (tipHeight >  m_bestBlockHeight.load()) {
                        m_bestBlockHeight.store(tipHeight);
                        requestBlock(tipHeight);
                    }
                }
            }
        }
    }
    else if (message.serviceId() == Api::APIService) {
        if (message.messageId() == Api::Meta::VersionReply) {
            Streaming::MessageParser parser(message.body());
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::Meta::GenericByteData) {
                    logCritical() << "Server is at version" << parser.stringData();
                    if (parser.stringData().compare("Flowee:1 (2019-9.1)") < 0) {
                        logFatal() << "  Hub server is too old";
                        m_network.punishNode(message.remote, 1000); // instant disconnect.
                    }
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
                logWarning() << "Failed to get block, hub didn't have it.";
                m_lastRequestedBlock = 0;
            }
            else logCritical() << "Failure detected" << serviceId << messageId;
        }
    }
    else if (message.serviceId() == Api::BlockNotificationService && message.messageId() == Api::BlockNotification::NewBlockOnChain) {
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockNotification::BlockHeight) {
                m_bestBlockHeight.store(parser.intData());
                requestBlock(parser.intData());
            }
        }
    }
    else {
        Streaming::MessageParser::debugMessage(message);
    }
}
