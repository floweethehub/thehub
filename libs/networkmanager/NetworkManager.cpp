/*
 * This file is part of the Flowee project
 * Copyright (C) 2016,2019-2020 Tom Zander <tomz@freedommail.ch>
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
#include "NetworkManager.h"
#include "NetworkManager_p.h"
#include "NetworkQueueFullError.h"
#include "NetworkServiceBase.h"
#include <NetworkEnums.h>
#include <APIProtocol.h>

#include <utils/hash.h>
#include <utils/streaming/MessageBuilder.h>
#include <utils/streaming/MessageParser.h>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>

#include <fstream>

// #define DEBUG_CONNECTIONS

constexpr int RECEIVE_STREAM_SIZE = 200000;
constexpr int CHUNK_SIZE = 8000;
constexpr int MAX_MESSAGE_SIZE = 9000;
constexpr int LEGACY_HEADER_SIZE = 24;

namespace {

thread_local Streaming::BufferPool m_buffer(10240); // used only really for message-headers
inline Streaming::BufferPool &pool(int reserveSize) {
    m_buffer.reserve(reserveSize);
    return m_buffer;
}

int reconnectTimeoutForStep(short step) {
    if (step < 5)
        return step*step*step / 2;
    return 44;
}

Message buildPingMessage(bool outgoingConnection) {
    Streaming::MessageBuilder builder(Streaming::HeaderOnly, 10);
    builder.add(Network::ServiceId, Network::SystemServiceId);
    if (outgoingConnection) // outgoing connections ping
        builder.add(Network::Ping, true);
    else
        builder.add(Network::Pong, true);
    builder.add(Network::HeaderEnd, true);
    return builder.message();
}

}


NetworkManager::NetworkManager(boost::asio::io_service &service)
    : d(std::make_shared<NetworkManagerPrivate>(service))
{
}

NetworkManager::~NetworkManager()
{
    std::lock_guard<std::recursive_mutex> lock(d->mutex);
    d->isClosingDown = true;
    for (auto server : d->servers) {
        server->shutdown();
    }
    for (auto it = d->connections.begin(); it != d->connections.end(); ++it) {
        it->second->shutdown();
    }
    d->connections.clear(); // invalidate NetworkConnection references
    for (auto service : d->services) {
        service->setManager(nullptr);
    }
    d->services.clear();
    d->unusedConnections.clear();
}

NetworkConnection NetworkManager::connection(const EndPoint &remote, ConnectionEnum connect)
{
    const bool hasHostname = (!remote.ipAddress.is_unspecified() || !remote.hostname.empty()) && remote.announcePort > 0;

    if (hasHostname) {
        std::lock_guard<std::recursive_mutex> lock(d->mutex);
        for (auto iter1 = d->connections.begin(); iter1 != d->connections.end(); ++iter1) {
            EndPoint endPoint = iter1->second->endPoint();
            if (!remote.hostname.empty() && endPoint.hostname != remote.hostname)
                continue;
            if (!remote.ipAddress.is_unspecified() && endPoint.ipAddress != remote.ipAddress)
                continue;
            if (!(endPoint.announcePort == 0 || endPoint.announcePort == remote.announcePort || remote.announcePort == 0))
                continue;
            return NetworkConnection(this, iter1->first);
        }

        if (connect == AutoCreate) {
            EndPoint ep(remote);
            if (ep.ipAddress.is_unspecified()) // try to see if hostname is an IP. If so, bypass DNS lookup
                try { ep.ipAddress = boost::asio::ip::address::from_string(ep.hostname); } catch (...) {}
            ep.peerPort = ep.announcePort; // outgoing connections always have those the same.

            ep.connectionId = ++d->lastConnectionId;
            if (d->unusedConnections.empty()) {
                d->connections.insert(std::make_pair(ep.connectionId, std::make_shared<NetworkManagerConnection>(d, ep)));
            } else {
                auto con = d->unusedConnections.front();
                d->unusedConnections.pop_front();
                con->setEndPoint(ep);
                d->connections.insert(std::make_pair(ep.connectionId, con));
            }

            return NetworkConnection(this, ep.connectionId);
        }
    }
    return NetworkConnection();
}

EndPoint NetworkManager::endPoint(int remoteId) const
{
    std::lock_guard<std::recursive_mutex> lock(d->mutex);
    NetworkManagerConnection *con = d->connections.at(remoteId).get();
    if (con)
        return con->endPoint();
    return EndPoint();
}

void NetworkManager::punishNode(int remoteId, int punishment)
{
    d->punishNode(remoteId, punishment);
}

void NetworkManager::bind(const tcp::endpoint &endpoint, const std::function<void(NetworkConnection&)> &callback)
{
    std::lock_guard<std::recursive_mutex> lock(d->mutex);
    try {
        NetworkManagerServer *server = new NetworkManagerServer(d, endpoint, callback);
        d->servers.push_back(server);
    } catch (std::exception &ex) {
        logWarning(Log::NWM) << "Creating NetworkMangerServer failed with" << ex.what();
        throw std::runtime_error("Failed to bind to endpoint");
    }

    if (d->servers.size() == 1) // start cron
        d->cronHourly(boost::system::error_code());
}

void NetworkManager::bind(const tcp::endpoint &endpoint)
{
    bind(endpoint, std::bind(&NetworkManagerPrivate::alwaysConnectingNewConnectionHandler, d, std::placeholders::_1));
}

void NetworkManager::addService(NetworkServiceBase *service)
{
    assert(service);
    if (!service) return;
    std::lock_guard<std::recursive_mutex> lock(d->mutex);
    d->services.push_back(service);
    service->setManager(this);
}

void NetworkManager::removeService(NetworkServiceBase *service)
{
    assert(service);
    if (!service) return;
    std::lock_guard<std::recursive_mutex> lock(d->mutex);
    d->services.remove(service);
    service->setManager(nullptr);
}

void NetworkManager::setMessageIdLookup(const std::map<int, std::string> &table)
{
    d->messageIds = table;
    d->messageIdsReverse.clear();
    for (auto iter = table.begin(); iter != table.end(); ++iter) {
        d->messageIdsReverse.insert(std::make_pair(iter->second, iter->first));
    }
}

void NetworkManager::setLegacyNetworkId(const std::vector<uint8_t> &magic)
{
    assert(magic.size() == 4);
    for (size_t i = 0; i < 4; ++i)
        d->networkId[i] = magic[i];
}

std::weak_ptr<NetworkManagerPrivate> NetworkManager::priv()
{
    return d;
}


/////////////////////////////////////


NetworkManagerPrivate::NetworkManagerPrivate(boost::asio::io_service &service)
    : ioService(service),
     lastConnectionId(0),
     isClosingDown(false),
     m_cronHourly(service)
{
}

NetworkManagerPrivate::~NetworkManagerPrivate()
{
    m_cronHourly.cancel();
}

void NetworkManagerPrivate::punishNode(int connectionId, int punishScore)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto con = connections.find(connectionId);
    if (con == connections.end())
        return;
    con->second->m_punishment += punishScore;

    if (con->second->m_punishment >= 1000) {
        BannedNode bn;
        bn.endPoint = con->second->endPoint();
        bn.banTimeout = boost::posix_time::second_clock::universal_time() + boost::posix_time::hours(24);
        logInfo(Log::NWM) << "Banned node for 24 hours due to excessive bad behavior" << bn.endPoint.hostname;
        banned.push_back(bn);
        connections.erase(connectionId);
        con->second->shutdown();
    }
}

void NetworkManagerPrivate::cronHourly(const boost::system::error_code &error)
{
    if (error)
        return;

    logDebug(Log::NWM) << "cronHourly";
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (isClosingDown)
        return;

    const auto now = boost::posix_time::second_clock::universal_time();
    std::list<BannedNode>::iterator bannedNode = banned.begin();
    // clean out banned nodes
    while (bannedNode != banned.end()) {
        if (bannedNode->banTimeout < now)
            bannedNode = banned.erase(bannedNode);
        else
            ++bannedNode;
    }
    for (auto connection : connections) {
        connection.second->m_punishment = std::max<short>(0, connection.second->m_punishment - 100);
        // logDebug(Log::NWM) << "peer ban scrore;" << connection.second->m_punishment;
    }
    m_cronHourly.expires_from_now(boost::posix_time::hours(1));
    m_cronHourly.async_wait(std::bind(&NetworkManagerPrivate::cronHourly, this, std::placeholders::_1));
}


/////////////////////////////////////

NetworkManagerConnection::NetworkManagerConnection(const std::shared_ptr<NetworkManagerPrivate> &parent, tcp::socket socket, int connectionId)
    : m_strand(parent->ioService),
    d(parent),
    m_socket(std::move(socket)),
    m_resolver(parent->ioService),
    m_lastCallbackId(1),
    m_isClosingDown(false),
    m_isConnected(true),
    m_reconnectDelay(parent->ioService),
    m_pingTimer(parent->ioService),
    m_sendTimer(parent->ioService),
    m_chunkedMessageBuffer(0)
{
    m_remote.ipAddress = m_socket.remote_endpoint().address();
    m_remote.announcePort = m_socket.remote_endpoint().port();
    m_remote.hostname = m_remote.ipAddress.to_string();
    m_remote.peerPort = 0;
    m_remote.connectionId = connectionId;
}

NetworkManagerConnection::NetworkManagerConnection(const std::shared_ptr<NetworkManagerPrivate> &parent, const EndPoint &remote)
    : m_strand(parent->ioService),
    d(parent),
    m_remote(remote),
    m_socket(parent->ioService),
    m_resolver(parent->ioService),
    m_receiveStream(RECEIVE_STREAM_SIZE),
    m_lastCallbackId(1),
    m_isClosingDown(false),
    m_isConnected(false),
    m_reconnectDelay(parent->ioService),
    m_pingTimer(parent->ioService),
    m_sendTimer(parent->ioService)
{
    if (m_remote.peerPort == 0)
        m_remote.peerPort = m_remote.announcePort;
}

void NetworkManagerConnection::connect()
{
    m_isClosingDown.store(false);
    runOnStrand(std::bind(&NetworkManagerConnection::connect_priv, shared_from_this()));
}

void NetworkManagerConnection::connect_priv()
{
    assert(m_strand.running_in_this_thread());
    assert(m_remote.announcePort == m_remote.peerPort); // its an outgoing connection
    if (m_isConnecting)
        return;
    if (m_isClosingDown)
        return;
    m_isConnecting = true;
    allocateBuffers();

    if (m_remote.ipAddress.is_unspecified()) {
        tcp::resolver::query query(m_remote.hostname, boost::lexical_cast<std::string>(m_remote.announcePort));
        m_resolver.async_resolve(query, m_strand.wrap(std::bind(&NetworkManagerConnection::onAddressResolveComplete,
                            shared_from_this(), std::placeholders::_1, std::placeholders::_2)));
    } else {
        if (m_remote.hostname.empty())
            m_remote.hostname = m_remote.ipAddress.to_string();
        boost::asio::ip::tcp::endpoint endpoint(m_remote.ipAddress, m_remote.announcePort);
        std::lock_guard<std::mutex> lock(d->connectionMutex);
        m_socket = boost::asio::ip::tcp::socket(d->ioService);
        m_socket.async_connect(endpoint, m_strand.wrap(
           std::bind(&NetworkManagerConnection::onConnectComplete, shared_from_this(), std::placeholders::_1)));
    }
}

void NetworkManagerConnection::onAddressResolveComplete(const boost::system::error_code &error, tcp::resolver::iterator iterator)
{
    if (m_isClosingDown)
        return;
    if (error) {
        logWarning(Log::NWM).nospace() << "connect[" << m_remote << "] " << error.message() << " (" << error.value() << ")";
        m_isConnecting = false;
        m_reconnectDelay.expires_from_now(boost::posix_time::seconds(45));
        m_reconnectDelay.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::reconnectWithCheck,
                            shared_from_this(), std::placeholders::_1)));
        errorDetected(error);
        return;
    }
    assert(m_strand.running_in_this_thread());
    // Notice that we always only use the first reported DNS entry. Which is likely Ok.
    m_remote.ipAddress = iterator->endpoint().address();
    logInfo(Log::NWM) << "Outgoing connection to" << m_remote.hostname << "resolved to:" << m_remote.ipAddress.to_string();

    std::lock_guard<std::mutex> lock(d->connectionMutex);
    m_socket = boost::asio::ip::tcp::socket(d->ioService);
    m_socket.async_connect(*iterator, m_strand.wrap(
       std::bind(&NetworkManagerConnection::onConnectComplete, shared_from_this(), std::placeholders::_1)));
}

void NetworkManagerConnection::onConnectComplete(const boost::system::error_code& error)
{
    if (m_isClosingDown)
        return;
    m_isConnecting = false;
    if (error) {
        if (error == boost::asio::error::operation_aborted) return;
        logWarning(Log::NWM).nospace() << "connect[" << m_remote.hostname.c_str() << ":" << m_remote.announcePort
                                    << "] (" << error.message() << ")";
        if (m_remote.peerPort != m_remote.announcePort) // incoming connection
            return;
        m_reconnectDelay.expires_from_now(boost::posix_time::seconds(reconnectTimeoutForStep(++m_reconnectStep)));
        m_reconnectDelay.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::reconnectWithCheck,
                                                            shared_from_this(), std::placeholders::_1)));
        errorDetected(error);
        return;
    }
    m_isConnected = true;
    assert(m_strand.running_in_this_thread());
    logInfo(Log::NWM) << "Successfully made TCP connection to" << m_remote.hostname.c_str() << m_remote.announcePort;

    for (auto it = m_onConnectedCallbacks.begin(); it != m_onConnectedCallbacks.end(); ++it) {
        try {
            it->second(m_remote);
        } catch (const std::exception &ex) {
            logWarning(Log::NWM) << "onConnected threw exception, ignoring:" << ex.what();
        }
    }

    runMessageQueue();
    requestMoreBytes(); // setup a callback for receiving.

    // for outgoing connections, ping. Notice that I don't care if they pong, as long as the TCP connection stays open
    if (m_messageHeaderType == FloweeNative) {
        m_pingTimer.expires_from_now(boost::posix_time::seconds(90));
        m_pingTimer.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::sendPing, shared_from_this(),
                                                       std::placeholders::_1)));
    }
}

Streaming::ConstBuffer NetworkManagerConnection::createHeader(const Message &message)
{
    assert(message.serviceId() >= 0);
    if (message.serviceId() == Api::LegacyP2P) {
        const auto body = message.body();

        auto &sendHelperBuffer = pool(4 + 12 + 4 + 4);
        memcpy(sendHelperBuffer.data(), d->networkId, 4);
        sendHelperBuffer.markUsed(4);
        auto m = d->messageIds.find(message.messageId());
        std::string messageId;
        if (m != d->messageIds.end())
            messageId = m->second;
        else
            logCritical() << "createHeader[legacy]: P2P message Id unknown:" << message.messageId();
        assert(messageId.size() <= 12);
        memcpy(sendHelperBuffer.data(), messageId.c_str(), messageId.size());
        for (size_t i = messageId.size(); i < 12; ++i) { // rest of version is zero-filled
            sendHelperBuffer.data()[i] = 0;
        }
        sendHelperBuffer.markUsed(12);
        const uint32_t messageSize = body.size();
        WriteLE32(reinterpret_cast<uint8_t*>(sendHelperBuffer.data()), messageSize);
        sendHelperBuffer.markUsed(4);

        uint256 hash = Hash(body.begin(), body.end());
        unsigned int checksum = 0;
        memcpy(&checksum, &hash, 4);
        WriteLE32(reinterpret_cast<uint8_t*>(sendHelperBuffer.data()), checksum);
        return sendHelperBuffer.commit(4);
    }
    else {
        const auto map = message.headerData();
        auto &sendHelperBuffer = pool(10 * static_cast<int>(map.size()));
        Streaming::MessageBuilder builder(sendHelperBuffer, Streaming::HeaderOnly);
        auto iter = map.begin();
        while (iter != map.end()) {
            assert(iter->first >= 0);
            builder.add(static_cast<uint32_t>(iter->first), iter->second);
            ++iter;
        }
        builder.add(Network::HeaderEnd, true);
        assert(sendHelperBuffer.size() + message.size() < MAX_MESSAGE_SIZE);
        builder.setMessageSize(sendHelperBuffer.size() + message.size());
        logDebug(Log::NWM) << "createHeader of message of length;" << sendHelperBuffer.size() << '+' << message.size();
        return builder.buffer();
    }
}

void NetworkManagerConnection::errorDetected(const boost::system::error_code &error)
{
    if (error == boost::asio::error::operation_aborted || !error) // no need to push those up the stack
        return;
    std::vector<std::function<void(int,const boost::system::error_code& error)> > callbacks;
    callbacks.reserve(m_onErrorCallbacks.size());
    for (auto it = m_onErrorCallbacks.begin(); it != m_onErrorCallbacks.end(); ++it) {
        callbacks.push_back(it->second);
    }
    for (auto callback : callbacks) {
        try {
            callback(m_remote.connectionId, error);
        } catch (const std::exception &e) {
            logCritical(Log::NWM) << "Callback 'onError' threw with" << e;
        }
    }
}

void NetworkManagerConnection::runMessageQueue()
{
    assert(m_strand.running_in_this_thread());
    if (m_sendingInProgress || (m_messageQueue->isRead() && m_priorityMessageQueue->isRead()) || !isConnected())
        return;

    m_sendingInProgress = true;

    /*
     * This method will schedule sending of data.
     * The data to send is pushed async to the network stack and the callback will come in essentially
     * the moment the network stack has accepted the data.  This is not at all any confirmation that
     * the other side accepted it!
     * But at the same time, the network stack has limited buffers and will only push to the network
     * an amount based on the TCP window size. So at minimum we know that the speed with which we
     * send stuff is indicative of the throughput.
     *
     * The idea here is to send a maximum amount of 250KB at a time. Which should be enough to avoid
     * delays. The speed limiter here mean we still allow messages that were pushed to the front of the
     * queue to be handled at a good speed.
     */
    int bytesLeft = 250*1024;
    std::vector<Streaming::ConstBuffer> socketQueue; // the stuff we will send over the socket

    while (m_priorityMessageQueue->hasUnread()) {
        const Message &message = m_priorityMessageQueue->unreadTip();
        if (m_sendQHeaders->isFull())
            break;
        int headerSize;
        if (message.hasHeader()) {
            headerSize = message.header().size();
        } else { // build a simple header
            const Streaming::ConstBuffer constBuf = createHeader(message);
            headerSize = constBuf.size();
            bytesLeft -= headerSize;
            socketQueue.push_back(constBuf);
            m_sendQHeaders->append(constBuf);
        }
        assert(message.body().size() + headerSize < MAX_MESSAGE_SIZE);
        socketQueue.push_back(message.rawData());
        bytesLeft -= message.rawData().size();
        m_priorityMessageQueue->markRead();
        if (bytesLeft <= 0)
            break;
    }

    while (m_messageQueue->hasUnread()) {
        if (bytesLeft <= 0)
            break;
        if (m_sendQHeaders->isFull())
            break;
        const Message &message = m_messageQueue->unreadTip();
        if (message.rawData().size() > CHUNK_SIZE && message.serviceId() != Api::LegacyP2P) {
            assert(!message.hasHeader()); // should have been blocked from entering in queueMessage();

            /*
             * The maximum size of a message is 9KB. This helps a lot with memory allocations and zero-copy ;)
             * A large message is then split into smaller ones and send with individual headers
             * to the other side where they can be re-connected.
             */
            Streaming::ConstBuffer body(message.body());
            const char *begin = body.begin() + m_messageBytesSend;
            const char *end = body.end();
            Streaming::ConstBuffer chunkHeader;// the first and last are different, but all the ones in the middle are duplicates.
            bool first = m_messageBytesSend == 0;
            while (begin < end) {
                const char *p = begin + CHUNK_SIZE;
                if (p > end)
                    p = end;
                m_messageBytesSend += p - begin;
                Streaming::ConstBuffer bodyChunk(body.internal_buffer(), begin, p);
                begin = p;

                Streaming::ConstBuffer header;
                if (first || begin == end || !chunkHeader.isValid()) {
                    const auto headerData = message.headerData();
                    auto &sendHelperBuffer = pool(20 + 8 * headerData.size());
                    Streaming::MessageBuilder headerBuilder(sendHelperBuffer, Streaming::HeaderOnly);
                    headerBuilder.add(Network::ServiceId, message.serviceId());
                    if (first) {
                        for (auto iter = headerData.begin(); iter != headerData.end(); ++iter) {
                            if (iter->first == Network::ServiceId) // forced to be first.
                                continue;
                            headerBuilder.add(iter->first, iter->second);
                        }
                        headerBuilder.add(Network::SequenceStart, body.size());
                    } else if (message.messageId() >= 0) {
                        headerBuilder.add(Network::MessageId, message.messageId());
                    }
                    headerBuilder.add(Network::LastInSequence, (begin == end));
                    headerBuilder.add(Network::HeaderEnd, true);
                    assert(sendHelperBuffer.size() + bodyChunk.size() < MAX_MESSAGE_SIZE);
                    headerBuilder.setMessageSize(sendHelperBuffer.size() + bodyChunk.size());

                    header = headerBuilder.buffer();
                    if (!first)
                        chunkHeader = header;
                    first = false;
                } else {
                    header = chunkHeader;
                }
                bytesLeft -= header.size();
                socketQueue.push_back(header);
                m_sendQHeaders->append(header);

                socketQueue.push_back(bodyChunk);
                bytesLeft -= bodyChunk.size();

                if (bytesLeft <= 0)
                    break;
            }
            if (begin >= end) { // done with message.
                m_messageBytesSend = 0;
                m_messageQueue->markRead();
            }
        }
        else {
            if (!message.hasHeader()) { // build a simple header
                const Streaming::ConstBuffer constBuf = createHeader(message);
                bytesLeft -= constBuf.size();
                socketQueue.push_back(constBuf);
                m_sendQHeaders->append(constBuf);
            }
            socketQueue.push_back(message.rawData());
            bytesLeft -= message.rawData().size();
            m_messageQueue->markRead();
        }
    }
    assert(m_messageBytesSend >= 0);

    boost::asio::async_write(m_socket, socketQueue,
        m_strand.wrap(std::bind(&NetworkManagerConnection::sentSomeBytes, shared_from_this(),
                                std::placeholders::_1, std::placeholders::_2)));
}

void NetworkManagerConnection::sentSomeBytes(const boost::system::error_code& error, std::size_t bytes_transferred)
{
    if (m_isClosingDown)
        return;

    m_sendingInProgress = false;
    if (error) {
        logWarning(Log::NWM) << "send received error" << error.message();
        m_messageBytesSend = 0;
        m_messageBytesSent = 0;
        m_sendQHeaders->clear();
        m_messageQueue->markAllUnread();
        m_priorityMessageQueue->markAllUnread();
        runOnStrand(std::bind(&NetworkManagerConnection::connect, shared_from_this()));
        return;
    }
    assert(m_strand.running_in_this_thread());
    if (!m_socket.is_open())
        return;
    logDebug(Log::NWM) << "Managed to send" << bytes_transferred << "bytes";
    m_reconnectStep = 0;

    m_messageQueue->removeAllRead();
    m_priorityMessageQueue->removeAllRead();
    m_sendQHeaders->clear();

    runMessageQueue();

    // if we interrupted the received-message-processing, resume that now.
    if (m_receiveStream.size() > 4) {
        const unsigned int rawHeader = *(reinterpret_cast<const unsigned int*>(m_receiveStream.begin()));
        const int packetLength = (rawHeader & 0xFFFF);
        if (packetLength <= m_receiveStream.size()) {
            logDebug() << "Resuming processing. ReceiveStream-size:"
                       << m_receiveStream.size() << "holds packet:" << packetLength
                       << "Message Queue now:" << m_messageQueue->size();
            receivedSomeBytes(boost::system::error_code(), 0);
        }
    }
}

void NetworkManagerConnection::receivedSomeBytes(const boost::system::error_code& error, std::size_t bytes_transferred)
{
    if (m_isClosingDown)
        return;
    if (error || m_receiveStream.begin() == nullptr) {
        logDebug(Log::NWM) << "receivedSomeBytes errored:" << error.message();
        // first copy to avoid problems if a callback removes its callback or closes the connection.
        std::vector<std::function<void(const EndPoint&)> > callbacks;
        callbacks.reserve(m_onDisConnectedCallbacks.size());
        for (auto it = m_onDisConnectedCallbacks.begin(); it != m_onDisConnectedCallbacks.end(); ++it) {
            callbacks.push_back(it->second);
        }

        for (auto callback : callbacks) {
            try {
                callback(m_remote);
            } catch (const std::exception &ex) {
                logInfo(Log::NWM) << "onDisconnected caused exception, ignoring:" << ex;
            }
        }
        close();
        return;
    }
    assert(m_strand.running_in_this_thread());
    m_receiveStream.markUsed(static_cast<int>(bytes_transferred)); // move write pointer

    while (true) { // get all packets out
        const size_t blockSize = static_cast<size_t>(m_receiveStream.size());
        if (blockSize < 4) // need more data
            break;

        // Check ring buffer capacity and send if low.
        if (m_messageQueue->size() > m_forceSendLimit) {
            logDebug(Log::NWM) << "Waiting with the processing of receive, too much outgoing queued";
            logDebug(Log::NWM) << " + Leaving" << m_receiveStream.size() << "bytes for later processing";
            runMessageQueue();
            return;
        }

        Streaming::ConstBuffer data = m_receiveStream.createBufferSlice(m_receiveStream.begin(), m_receiveStream.end());

        if (m_firstPacket) {
            m_firstPacket = false;
            if (m_messageHeaderType == FloweeNative) {
                if (data.begin()[2] != 8) { // Positive integer (0) and Network::ServiceId (1 << 3)
                    logWarning(Log::NWM) << "receive; Data error from remote - this is NOT an NWM server. Disconnecting" << m_remote.hostname;
                    disconnect();
                    return;
                }
            } else {
                assert(m_messageHeaderType == LegacyP2P);
                bool ok = true;
                for (size_t i = 0; ok && i < 4; ++i)
                    ok |= data.begin()[i] == d->networkId[i];
                if (!ok) {
                    logWarning(Log::NWM) << "receive; Data error from remote - this is NOT an P@P server. Disconnecting" << m_remote.hostname;
                    disconnect();
                    return;
                }
            }
        }
        if (m_messageHeaderType == LegacyP2P) {
            if (data.size() < LEGACY_HEADER_SIZE) // wait for entire header
                break;

            const uint32_t bodyLength = ReadLE32(reinterpret_cast<const uint8_t*>(data.begin() + 16));
            if (bodyLength > 32000000) {
                logWarning(Log::NWM).nospace() << "receive; Data error from server - stream is corrupt ("
                                     << "bl=" << bodyLength << ")";
                close(false);
                return;
            }
            if (data.size() < LEGACY_HEADER_SIZE + static_cast<int>(bodyLength)) // do we have all data for this one?
                break;

            if (!processLegacyPacket(m_receiveStream.internal_buffer(), data.begin()))
                return;
            m_receiveStream.forget(bodyLength + LEGACY_HEADER_SIZE);
        }
        else {
            const unsigned int rawHeader = *(reinterpret_cast<const unsigned int*>(data.begin()));
            const int packetLength = (rawHeader & 0xFFFF);
            logDebug(Log::NWM) << "Processing incoming packet. Size" << packetLength;
            if (packetLength > MAX_MESSAGE_SIZE) {
                logWarning(Log::NWM).nospace() << "receive; Data error from server - stream is corrupt ("
                                               << "pl=" << packetLength << ")";
                close();
                return;
            }
            if (data.size() < packetLength) // do we have all data for this one?
                break;
            if (!processPacket(m_receiveStream.internal_buffer(), data.begin()))
                return;
            m_receiveStream.forget(packetLength);
        }
    }
    requestMoreBytes_callback(boost::system::error_code());
}

/*
 * when we generate more messages than can be sent, we start throttling the incoming
 * message flow. The basic thought is that more incoming messages means more outgoing
 * messages will be generated.
 * As such it makes sense to start slowing down what we sent in order to avoid memory buffers
 * for send-queues growing out of proportion.
 */
void NetworkManagerConnection::requestMoreBytes_callback(const boost::system::error_code &error)
{
    if (error)
        return;

    const int backlog = m_messageQueue->size() + m_priorityMessageQueue->size();
    if (backlog < m_throttleReceiveAtSendLimitL1)
        requestMoreBytes();
    else if (isConnected()) {
        int wait = 2;
        if (backlog > m_throttleReceiveAtSendLimitL3)
            wait = 30;
        else if (backlog > m_throttleReceiveAtSendLimitL2)
            wait = 10;
        m_sendTimer.expires_from_now(boost::posix_time::milliseconds(wait));
        m_sendTimer.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::requestMoreBytes_callback,
                                                       shared_from_this(), std::placeholders::_1)));
        runMessageQueue();
    }
}

void NetworkManagerConnection::requestMoreBytes()
{
    m_receiveStream.reserve(MAX_MESSAGE_SIZE);
    assert(m_receiveStream.capacity() > 0);
    m_socket.async_receive(boost::asio::buffer(m_receiveStream.data(), static_cast<size_t>(m_receiveStream.capacity())),
            m_strand.wrap(std::bind(&NetworkManagerConnection::receivedSomeBytes, shared_from_this(),
                                    std::placeholders::_1, std::placeholders::_2)));
}

bool NetworkManagerConnection::processPacket(const std::shared_ptr<char> &buffer, const char *data)
{
    assert(m_strand.running_in_this_thread());
    const unsigned int rawHeader = *(reinterpret_cast<const unsigned int*>(data));
    const int packetLength = (rawHeader & 0xFFFF);
    logDebug(Log::NWM) << "Receive packet length" << packetLength;

    const char *messageStart = data + 2;
    Streaming::MessageParser parser(Streaming::ConstBuffer(buffer, messageStart, messageStart + packetLength));
    Streaming::ParsedType type = parser.next();
    int headerSize = 0;
    int messageId = -1;
    int serviceId = -1;
    int lastInSequence = -1;
    int sequenceSize = -1;
    bool isPing = false;
    // TODO have a variable on the NetworkManger that indicates the maximum allowed combined message-size.

    std::map<int, int> messageHeaderData;
    bool inHeader = true;
    while (inHeader && type == Streaming::FoundTag) {
        switch (parser.tag()) {
        case Network::HeaderEnd:
            headerSize = parser.consumed();
            inHeader = false;
            break;
        case Network::MessageId:
            if (!parser.isInt()) {
                close();
                return false;
            }
            messageId = parser.intData();
            break;
        case Network::ServiceId:
            if (!parser.isInt()) {
                close();
                return false;
            }
            serviceId = parser.intData();
            break;
        case Network::LastInSequence:
            if (!parser.isBool()) {
                close();
                return false;
            }
            lastInSequence = parser.boolData() ? 1 : 0;
            break;
        case Network::SequenceStart:
            if (!parser.isInt()) {
                close();
                return false;
            }
            sequenceSize = parser.intData();
            break;
        case Network::Ping:
            isPing = true;
            break;
        default:
            if (parser.isInt() && parser.tag() < 0xFFFFFF) {
                if (parser.tag() <= 10) { // illegal header tag for users.
                    logInfo(Log::NWM) << "  header uses illegal tag. Malformed: re-connecting";
                    close();
                    return false;
                }
                messageHeaderData.insert(std::make_pair(static_cast<int>(parser.tag()), parser.intData()));
            }
            break;
        }

        type = parser.next();
    }
    if (inHeader) {
        logInfo(Log::NWM) << "  header malformed, re-connecting";
        close();
        return false;
    }

    if (serviceId == -1) { // an obligatory field
        logWarning(Log::NWM) << "peer sent message without serviceId";
        close();
        return false;
    }

    if (serviceId == Network::SystemServiceId) { // Handle System level messages
        if (isPing) {
            if (m_remote.peerPort == m_remote.announcePort) {
                // we should never get pings from a remote when we initiated the connection.
                disconnect();
                return false;
            }
            m_pingTimer.cancel();
            if (!m_messageQueue->isFull()) {
                queueMessage(m_pingMessage, NetworkConnection::NormalPriority);
                m_pingTimer.expires_from_now(boost::posix_time::seconds(120));
                m_pingTimer.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::pingTimeout, this, std::placeholders::_1)));
            }
        }
        return true;
    }

    Message message;
    // we assume they are in sequence (which is Ok with TCP sockets), but we don't assume that
    // each packet is part of the sequence.
    if (lastInSequence != -1) {
        if (sequenceSize != -1) {
            if (m_chunkedMessageId != -1 || m_chunkedServiceId != -1) { // Didn't finish another. Thats illegal.
                logWarning(Log::NWM) << "peer sent sequenced message with wrong combination of headers";
                close();
                return false;
            }
            m_chunkedMessageId = messageId;
            m_chunkedServiceId = serviceId;
            m_chunkedMessageBuffer = Streaming::BufferPool(sequenceSize);
            m_chunkedHeaderData = messageHeaderData;
        }
        else if (m_chunkedMessageId != messageId || m_chunkedServiceId != serviceId) { // Changed. Thats illegal.
            close();
            logWarning(Log::NWM) << "peer sent sequenced message with inconsistent service/messageId";
            return false;
        }
        const int bodyLength = packetLength - headerSize - 2;
        if (m_chunkedMessageBuffer.capacity() < bodyLength) {
            logWarning(Log::NWM) << "peer sent sequenced message with too much data";
            return false;
        }

        logDebug(Log::NWM) << "Message received as part of sequence; last:" << lastInSequence
                 << "total-size:" << sequenceSize;
        std::copy(data + headerSize + 2, data + packetLength, m_chunkedMessageBuffer.data());
        m_chunkedMessageBuffer.markUsed(bodyLength);
        if (lastInSequence == 0)
            return true;

        message = Message(m_chunkedMessageBuffer.commit(), m_chunkedServiceId);
        messageHeaderData = m_chunkedHeaderData;
        m_chunkedMessageId = -1;
        m_chunkedServiceId = -1;
        m_chunkedMessageBuffer.clear();
    }
    else {
        message = Message(buffer, data + 2, data + 2 + headerSize, data + packetLength);
    }
    message.setMessageId(messageId);
    message.setServiceId(serviceId);
    for (auto iter = messageHeaderData.begin(); iter != messageHeaderData.end(); ++iter) {
        message.setHeaderInt(iter->first, iter->second);
    }
    message.remote = m_remote.connectionId;

    // first copy to avoid problems if a callback removes its callback or closes the connection.
    std::vector<std::function<void(const Message&)> > callbacks;
    callbacks.reserve(m_onIncomingMessageCallbacks.size());
    for (auto it = m_onIncomingMessageCallbacks.begin(); it != m_onIncomingMessageCallbacks.end(); ++it) {
        callbacks.push_back(it->second);
    }

    for (auto callback : callbacks) {
        try {
            callback(message);
        } catch (const NetworkQueueFullError &e) {
            logDebug(Log::NWM) << "connection::onIncomingMessage tried to send, but failed (and didn't catch exception) dropping message" << e;
        } catch (const std::exception &ex) {
            logWarning(Log::NWM) << "connection::onIncomingMessage threw exception, ignoring:" << ex;
        }
        if (!m_socket.is_open())
            break;
    }
    std::list<NetworkServiceBase*> servicesCopy;
    {
        std::lock_guard<std::recursive_mutex> lock(d->mutex);
        servicesCopy = d->services;
    }
    for (auto service : servicesCopy) {
        if (!m_socket.is_open())
            break;
        if (service->id() == serviceId) {
            try {
                service->onIncomingMessage(message, m_remote);
            } catch (const std::exception &ex) {
                logWarning(Log::NWM) << "service::onIncomingMessage threw exception, ignoring:" << ex;
            }
        }
    }

    return m_socket.is_open(); // if the user called disconnect, then stop processing packages
}

bool NetworkManagerConnection::processLegacyPacket(const std::shared_ptr<char> &buffer, const char *data)
{
    assert(m_strand.running_in_this_thread());
    const int bodyLength = ReadLE32(reinterpret_cast<const uint8_t*>(data + 16));
    logDebug(Log::NWM) << "Receive legacy-packet Body-length:" << bodyLength;

    char buf[13];
    memcpy(buf, data + 4, 12);
    buf[12] = 0;
    auto m = d->messageIdsReverse.find(std::string(buf));
    if (m == d->messageIdsReverse.end()) {
        logWarning(Log::NWM) << "Incoming message has unknown type:" << std::string(data + 4, 12);
        return true; // skip
    }
    Message message(buffer, data, data + LEGACY_HEADER_SIZE, data + LEGACY_HEADER_SIZE + bodyLength);

    message.setMessageId(m->second);
    message.setServiceId(Api::LegacyP2P);
    message.remote = m_remote.connectionId;

    // first copy to avoid problems if a callback removes its callback or closes the connection.
    std::vector<std::function<void(const Message&)> > callbacks;
    callbacks.reserve(m_onIncomingMessageCallbacks.size());
    for (auto it = m_onIncomingMessageCallbacks.begin(); it != m_onIncomingMessageCallbacks.end(); ++it) {
        callbacks.push_back(it->second);
    }

    for (auto callback : callbacks) {
        try {
            callback(message);
        } catch (const NetworkQueueFullError &e) {
            logDebug(Log::NWM) << "connection::onIncomingMessage tried to send, but failed (and didn't catch exception) dropping message" << e;
        } catch (const std::exception &ex) {
            logWarning(Log::NWM) << "connection::onIncomingMessage (LegacyP2P) threw exception, ignoring:" << ex;
        }
        if (!m_socket.is_open())
            break;
    }

    return m_socket.is_open(); // if the user called disconnect, then stop processing packages
}

void NetworkManagerConnection::addOnConnectedCallback(int id, std::function<void(const EndPoint&)> callback)
{
    assert(m_strand.running_in_this_thread());
    m_onConnectedCallbacks.insert(std::make_pair(id, callback));
}

void NetworkManagerConnection::addOnDisconnectedCallback(int id, std::function<void(const EndPoint&)> callback)
{
    assert(m_strand.running_in_this_thread());
    m_onDisConnectedCallbacks.insert(std::make_pair(id, callback));
}

void NetworkManagerConnection::addOnIncomingMessageCallback(int id, std::function<void(const Message&)> callback)
{
    assert(m_strand.running_in_this_thread());
    m_onIncomingMessageCallbacks.insert(std::make_pair(id, callback));
}

void NetworkManagerConnection::addOnError(int id, std::function<void (int, const boost::system::error_code &)> callback)
{
    assert(m_strand.running_in_this_thread());
    m_onErrorCallbacks.insert(std::make_pair(id, callback));
}

void NetworkManagerConnection::queueMessage(const Message &message, NetworkConnection::MessagePriority priority)
{
    if (!message.hasHeader() && message.serviceId() == -1)
        throw NetworkException("queueMessage: Can't deliver a message with unset service ID");
    if (message.hasHeader() && message.body().size() > CHUNK_SIZE)
        throw NetworkException("queueMessage: Can't send large message and can't auto-chunk because it already has a header");
    if (priority != NetworkConnection::NormalPriority && message.rawData().size() > CHUNK_SIZE)
        throw NetworkException("queueMessage: Can't send large message in the priority queue");

    // we have a chunk size of 8K and a max message size of 9K. The 1000 bytes is for headers and worse case is around
    // 10 bytes per item plus some extra stuff. So we reject any messages with more than 95 header items.
    if (message.headerData().size() > 95)
        NetworkException("queueMessage: Can't send message with too much header items");

    if (m_strand.running_in_this_thread()) {
        allocateBuffers();
        if (priority == NetworkConnection::NormalPriority) {
            if (m_messageQueue->isFull())
                throw NetworkQueueFullError("MessageQueue full");
            m_messageQueue->append(message);
        } else {
            if (m_priorityMessageQueue->isFull())
                throw NetworkQueueFullError("PriorityMessageQueue full");
            m_priorityMessageQueue->append(message);
        }
        if (isConnected())
            runMessageQueue();
        else
            connect_priv();
    } else {
        runOnStrand(std::bind(&NetworkManagerConnection::queueMessage, this, message, priority));
    }
}

void NetworkManagerConnection::close(bool reconnect)
{
    assert(m_strand.running_in_this_thread());
    if (!isOutgoing()) {
        std::lock_guard<std::recursive_mutex> lock(d->mutex);
        shutdown();
        d->connections.erase(m_remote.connectionId);
        return;
    }
    if (!reconnect)
        m_isClosingDown = true;

    m_receiveStream.clear();
    m_chunkedMessageBuffer.clear();
    m_chunkedMessageId = -1;
    m_chunkedServiceId = -1;
    m_chunkedHeaderData.clear();
    m_messageBytesSend = 0;
    m_messageBytesSent = 0;
    m_reconnectDelay.cancel();
    m_resolver.cancel();
    m_sendQHeaders->clear();
    if (m_isConnected)
        m_socket.close();
    m_pingTimer.cancel();
    m_firstPacket = true;
    m_isConnected = false;
    m_isConnecting = false;
    if (reconnect && !m_isClosingDown) { // auto reconnect.
        if (m_firstPacket) { // this means the network is there, someone is listening. They just don't speak our language.
            // slow down reconnect due to bad peer.
            m_reconnectDelay.expires_from_now(boost::posix_time::seconds(15));
            m_reconnectDelay.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::reconnectWithCheck, this, std::placeholders::_1)));
        } else {
            connect_priv();
        }
    }
}

void NetworkManagerConnection::sendPing(const boost::system::error_code &error)
{
    if (error) return;
    logDebug(Log::NWM) << "ping";

    if (m_isClosingDown)
        return;
    assert (m_messageHeaderType != LegacyP2P);
    assert(m_strand.running_in_this_thread());
    if (!m_socket.is_open())
        return;
    int time = 90;
    if (m_messageQueue->isFull()) {
        if (m_priorityMessageQueue->isFull())
            time = 2; // delay sending ping
        else
            queueMessage(m_pingMessage, NetworkConnection::HighPriority);
    } else {
        queueMessage(m_pingMessage, NetworkConnection::NormalPriority);
    }
    m_pingTimer.expires_from_now(boost::posix_time::seconds(time));
    m_pingTimer.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::sendPing, this, std::placeholders::_1)));
}

void NetworkManagerConnection::pingTimeout(const boost::system::error_code &error)
{
    // note that this is only for incoming connections.
    if (!error) {
        logWarning(Log::NWM) << "Didn't receive a ping from peer for too long, disconnecting dead connection";
        disconnect();
    }
}

void NetworkManagerConnection::allocateBuffers()
{
    if (m_messageQueue.get() == nullptr || m_messageQueue->reserved() != m_queueSizeMain) {
        m_messageQueue.reset(new RingBuffer<Message>(m_queueSizeMain));
        m_priorityMessageQueue.reset(new RingBuffer<Message>(m_priorityQueueSize));
        m_sendQHeaders.reset(new RingBuffer<Streaming::ConstBuffer>(m_queueSizeMain));

        m_pingMessage = buildPingMessage(m_remote.peerPort == m_remote.announcePort);
    }
}

void NetworkManagerConnection::reconnectWithCheck(const boost::system::error_code& error)
{
    if (!error) {
        m_socket.close();
        connect_priv();
    }
}

int NetworkManagerConnection::nextCallbackId()
{
    return m_lastCallbackId.fetch_add(1);
}

void NetworkManagerConnection::removeAllCallbacksFor(int id)
{
    assert(m_strand.running_in_this_thread());
    m_onConnectedCallbacks.erase(id);
    m_onDisConnectedCallbacks.erase(id);
    m_onIncomingMessageCallbacks.erase(id);
    m_onErrorCallbacks.erase(id);
}

void NetworkManagerConnection::shutdown()
{
    m_isClosingDown = true;
    if (m_strand.running_in_this_thread()) {
        m_onConnectedCallbacks.clear();
        m_onDisConnectedCallbacks.clear();
        m_onIncomingMessageCallbacks.clear();
        m_onErrorCallbacks.clear();
        if (isConnected())
            m_socket.close();
        m_resolver.cancel();
        m_reconnectDelay.cancel();
        m_strand.post(std::bind(&NetworkManagerConnection::finalShutdown, shared_from_this()));
    } else {
        m_strand.post(std::bind(&NetworkManagerConnection::shutdown, shared_from_this()));
    }
}

void NetworkManagerConnection::accept()
{
    if (m_acceptedConnection)
        return;
    m_acceptedConnection = true;
    allocateBuffers();

    // setup a callback for receiving.
    m_socket.async_receive(boost::asio::buffer(m_receiveStream.data(), static_cast<size_t>(m_receiveStream.capacity())),
        m_strand.wrap(std::bind(&NetworkManagerConnection::receivedSomeBytes, shared_from_this(),
                                std::placeholders::_1, std::placeholders::_2)));

    // for incoming connections, take action when no ping comes in.
    m_pingTimer.expires_from_now(boost::posix_time::seconds(120));
    m_pingTimer.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::pingTimeout, this, std::placeholders::_1)));
}

void NetworkManagerConnection::recycleConnection()
{
    assert(m_strand.running_in_this_thread());
    m_onConnectedCallbacks.clear();
    m_onDisConnectedCallbacks.clear();
    m_onIncomingMessageCallbacks.clear();
    m_onErrorCallbacks.clear();
    setMessageQueueSizes(2000, 20); // set back to defaults.
    m_punishment = 0;
    close(false);
    std::lock_guard<std::recursive_mutex> lock(d->mutex); // protects connections maps
    if (d->connections.erase(m_remote.connectionId))
        d->unusedConnections.push_back(shared_from_this());
}

void NetworkManagerConnection::runOnStrand(const std::function<void()> &function)
{
    if (m_isClosingDown)
        return;
    m_strand.post(function);
}

void NetworkManagerConnection::punish(int amount)
{
    d->punishNode(m_remote.connectionId, amount);
}

void NetworkManagerConnection::setMessageQueueSizes(int main, int priority)
{
    m_queueSizeMain = main;
    m_priorityQueueSize = priority;

    // Calculate the limits. We only really use 'main' here
    // These numbers may be tweaked with some more testing, if someone wants to put the time in.
    m_forceSendLimit = main / 8 * 3;
    m_throttleReceiveAtSendLimitL1 = main / 2;
    m_throttleReceiveAtSendLimitL2 = main / 4 * 3;
    m_throttleReceiveAtSendLimitL3 = main - (main / 20);
}

void NetworkManagerConnection::setMessageHeaderType(MessageHeaderType messageHeaderType)
{
    if (m_messageHeaderType == messageHeaderType)
        return;
    m_messageHeaderType = messageHeaderType;
    switch (m_messageHeaderType) {
    case FloweeNative:
        if (isOutgoing()) {
            m_pingTimer.expires_from_now(boost::posix_time::seconds(30));
            m_pingTimer.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::sendPing, this, std::placeholders::_1)));
        }
        break;
    case LegacyP2P:
        m_pingTimer.cancel();
        break;
    default:
        assert(false);
        break;
    }
}

void NetworkManagerConnection::finalShutdown()
{
}

NetworkManagerServer::NetworkManagerServer(const std::shared_ptr<NetworkManagerPrivate> &parent, tcp::endpoint endpoint, const std::function<void(NetworkConnection&)> &callback)
    : d(parent),
      m_acceptor(parent->ioService, endpoint),
      m_socket(parent->ioService),
      onIncomingConnection(callback)
{
    setupCallback();
}

void NetworkManagerServer::shutdown()
{
    m_socket.close();
}

void NetworkManagerServer::setupCallback()
{
    m_acceptor.async_accept(m_socket, std::bind(&NetworkManagerServer::acceptConnection, this, std::placeholders::_1));
}

void NetworkManagerServer::acceptConnection(boost::system::error_code error)
{
    if (error.value() == boost::asio::error::operation_aborted)
        return;
    logDebug(Log::NWM) << "acceptTcpConnection" << error.message();
    if (error) {
        setupCallback();
        return;
    }
    std::shared_ptr<NetworkManagerPrivate> priv = d.lock();
    if (!priv.get())
        return;

    std::lock_guard<std::recursive_mutex> lock(priv->mutex);
    if (priv->isClosingDown)
        return;

    struct RIAA {
        RIAA(NetworkManagerServer *p) : parent(p) {}
        ~RIAA() {
            parent->setupCallback();
        }
        NetworkManagerServer *parent;
    };
    RIAA riaa(this);

    try {
        // catch ENOTCONN (Transport endpoint is not connected) which remote_endpoint() may throw
        const boost::asio::ip::address peerAddress = m_socket.remote_endpoint().address();
        for (const BannedNode &bn : priv->banned) {
            if (bn.endPoint.ipAddress == peerAddress) {
                if (bn.banTimeout > boost::posix_time::second_clock::universal_time()) { // incoming connection is banned.
                    logInfo(Log::NWM) << "acceptTcpConnection; closing incoming connection (banned)"
                              << bn.endPoint.hostname;
                    m_socket.close();
                }
                return;
            }
        }

        const int conId = ++priv->lastConnectionId;
        logDebug(Log::NWM) << "acceptTcpConnection; creating new connection object" << conId;
        // Never do a setupCallback until we do a 'std::move' (or disconnect)  to avoid an "Already open" error
        std::shared_ptr<NetworkManagerConnection> connection = std::make_shared<NetworkManagerConnection>(priv, std::move(m_socket), conId);
        priv->connections.insert(std::make_pair(conId, connection));
        logDebug(Log::NWM) << "Total connections now;" << priv->connections.size();

        NetworkConnection con(connection, conId);
        try {
            onIncomingConnection(con);
        } catch (const std::exception &e) {
            logCritical(Log::NWM) << "subsystem handling onIncomingConnection threw. Ignoring" << e;
        }

        // someone needs to call accept(), if they didn't we shall disconnect
        if (!connection->acceptedConnection())
            connection->m_strand.post(std::bind(&NetworkManagerConnection::disconnect, connection));

    } catch (...) {
        logInfo(Log::NWM) << "AcceptConnection found that peer closed before we could handle it.";
        try { m_socket.close(); } catch (...) {} // TODO do we need this?
    }
}
