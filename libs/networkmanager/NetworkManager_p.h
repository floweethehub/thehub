/*
 * This file is part of the Flowee project
 * Copyright (C) 2016, 2019-2020 Tom Zander <tomz@freedommail.ch>
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
#ifndef NETWORKMANAGER_P_H
#define NETWORKMANAGER_P_H

/*
 * WARNING USAGE OF THIS HEADER IS RESTRICTED.
 * This Header file is part of the private API and is meant to be used solely by the NetworkManager component.
 *
 * Usage of this API will likely mean your code will break in interesting ways in the future,
 * or even stop to compile.
 *
 * YOU HAVE BEEN WARNED!!
 */

#include "NetworkEndPoint.h"
#include "NetworkConnection.h"
#include <Message.h>
#include <streaming/BufferPool.h>

#include <list>
#include <deque>
#include <atomic>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/asio.hpp>

class NetworkServiceBase;
class NetworkManagerPrivate;

using boost::asio::ip::tcp;

template<class V>
class RingBuffer
{
public:
    RingBuffer(size_t size) : m_array(size), NumItems(size) { }

    void append(const V &v) {
        m_array[m_next] = v;
        if (++m_next >= NumItems)
            m_next = 0;
        assert(m_next != m_first);
    }

    /// total amount of space in this ringbuffer
    inline int reserved() const { return NumItems; }
    /// Amount of items filled
    inline int count() const {
        return m_next - m_first + (m_next < m_first /* we went circular */ ? NumItems : 0);
    }
    /// reserved minus usage
    int slotsAvailable() const {
        return reserved() - count();
    }
    /// alias for count()
    inline int size() const { return count(); }
    inline bool isEmpty() const { return m_first == m_next; }

    /// the tip is the first inserted, but not yet removed item.
    inline const V &tip() const {
        assert(m_first != m_next); // aka empty
        return m_array[m_first];
    }
    /// remove the tip, moving the tip to the next item.
    inline void removeTip() {
        assert(m_first != m_next); // aka empty
        m_array[m_first] = V();
        if (++m_first >= NumItems)
            m_first = 0;

        if (m_first <= m_next) // standard linear list
            m_readIndex = std::max(m_readIndex, m_first);
        else if (m_readIndex < m_first && m_readIndex > m_next) // circular list state
            m_readIndex = m_first;
    }

    /// an item just inserted is unread, we read in the same order as insertion.
    inline void markRead(int count = 1) {
        assert(count < NumItems);
        assert(count > 0);
        m_readIndex += count;
        while (m_readIndex >= NumItems)
            m_readIndex -= NumItems;
        assert(m_first < m_next && m_readIndex >= m_first && m_readIndex <= m_next
               || (m_first > m_next && (m_readIndex >= m_first || m_readIndex <= m_next)));
    }

    inline void removeAllRead() {
        while (m_first != m_readIndex) {
            m_array[m_first] = V();
            if (++m_first >= NumItems)
                m_first = 0;
        }
    }
    /// first not yet read item.
    inline const V &unreadTip() const {
        assert(m_first < m_next && m_readIndex >= m_first && m_readIndex < m_next
               || (m_first > m_next && (m_readIndex >= m_first || m_readIndex < m_next)));
        return m_array[m_readIndex];
    }
    /// returns true, like isEmpty(), when there are no unread items.
    inline bool isRead() const { return m_readIndex == m_next; }
    /// Return true if there are items inserted, but not yet marked read (inverse of isRead())
    inline bool hasUnread() const { return m_readIndex != m_next; }
    inline bool isFull() const { return m_next + 1 == m_first; }

    inline bool hasItemsMarkedRead() const {
        return m_readIndex != m_first;
    }
    inline void markAllUnread() {
        m_readIndex = m_first;
    }

    /// clear all data.
    inline void clear() {
        const int end = count() + m_first;
        for (int i = m_first; i < end; ++i) {
            m_array[i % NumItems] = V();
        }
        m_first = 0;
        m_readIndex = 0;
        m_next = 0;
    }

private:
    /* We append at 'next', increasing it to point to the first unused one.
     * As this is a FIFO, we move the m_first and m_readIndex as we process (and remove) items,
     * if we reach the 'next' position we have an empty buffer.
     *
     * Write       |   variable  |   Read
     *
     *                 m_first   ->   tip()
     *                                 |
     * markRead() -> m_readIndex ->  unreadTip()
     *                                 |
     *                               last-item
     * append()   ->  m_next     ->   nullptr
     */
    std::vector<V> m_array;
    int m_first = 0;
    int m_readIndex = 0;
    int m_next = 0; // last plus one
    const size_t NumItems;
};

class NetworkManagerConnection : public std::enable_shared_from_this<NetworkManagerConnection>
{
public:
    enum MessageHeaderType {
        FloweeNative,
        LegacyP2P
    };

    NetworkManagerConnection(const std::shared_ptr<NetworkManagerPrivate> &parent, tcp::socket socket, int connectionId);
    NetworkManagerConnection(const std::shared_ptr<NetworkManagerPrivate> &parent, const EndPoint &remote);
    /// Connects to remote (async)
    void connect();

    int nextCallbackId();

    /// unregister a NetworkConnection. Calls have to be from the strand.
    void removeAllCallbacksFor(int id);

    void queueMessage(const Message &message, NetworkConnection::MessagePriority priority);

    inline bool isConnected() const {
        return m_isConnected;
    }

    inline const EndPoint &endPoint() const {
        return m_remote;
    }

    void setEndPoint(const EndPoint &ep) {
        m_remote = ep;
    }

    /// add callback, calls have to be on the strand.
    void addOnConnectedCallback(int id, std::function<void(const EndPoint&)> callback);
    /// add callback, calls have to be on the strand.
    void addOnDisconnectedCallback(int id, std::function<void(const EndPoint&)> callback);
    /// add callback, calls have to be on the strand.
    void addOnIncomingMessageCallback(int id, std::function<void(const Message&)> callback);
    /// add callback, calls have to be on the strand.
    void addOnError(int id, std::function<void(int,const boost::system::error_code&)> callback);

    /// forcably shut down the connection, soon you should no longer reference this object
    void shutdown();

    /// only incoming connections need accepting.
    void accept();

    inline void disconnect() {
        close(false);
        assert(m_priorityMessageQueue);
        m_priorityMessageQueue->clear();
        assert(m_messageQueue);
        m_messageQueue->clear();
    }

    void recycleConnection();

    boost::asio::io_context::strand m_strand;

    /// move a call to the thread that the strand represents
    void runOnStrand(const std::function<void()> &function);

    inline bool acceptedConnection() const {
        return m_acceptedConnection;
    }

    void setMessageHeaderType(MessageHeaderType messageHeaderType);

    void punish(int amount);
    inline void setMessageQueueSizes(int main, int priority) {
        m_queueSizeMain = main;
        m_priorityQueueSize = priority;
    }
    void close(bool reconnect = true); // close down connection

    short m_punishment = 0; // aka ban-sore
    // used to check incoming messages being actually for us
    MessageHeaderType m_messageHeaderType = FloweeNative;

    std::shared_ptr<NetworkManagerPrivate> d;

private:
    EndPoint m_remote;

    void onAddressResolveComplete(const boost::system::error_code& error, tcp::resolver::iterator iterator);
    void onConnectComplete(const boost::system::error_code& error);

    /// call then to start sending messages to remote. Will do noting if we are already sending messages.
    void runMessageQueue();
    void sentSomeBytes(const boost::system::error_code& error, std::size_t bytes_transferred);
    void requestMoreBytes_callback(const boost::system::error_code &error);
    void requestMoreBytes();
    void receivedSomeBytes(const boost::system::error_code& error, std::size_t bytes_transferred);

    bool processPacket(const std::shared_ptr<char> &buffer, const char *data);
    bool processLegacyPacket(const std::shared_ptr<char> &buffer, const char *data);
    void connect_priv(); // thread-unsafe version of connect
    void reconnectWithCheck(const boost::system::error_code& error); // called from the m_reconectDelay timer
    void finalShutdown();
    void sendPing(const boost::system::error_code& error);
    void pingTimeout(const boost::system::error_code& error);
    void allocateBuffers();

    inline bool isOutgoing() const {
        return m_remote.announcePort == m_remote.peerPort;
    }

    Streaming::ConstBuffer createHeader(const Message &message);

    void errorDetected(const boost::system::error_code& error);

    std::map<int, std::function<void(const EndPoint&)> > m_onConnectedCallbacks;
    std::map<int, std::function<void(const EndPoint&)> > m_onDisConnectedCallbacks;
    std::map<int, std::function<void(const Message&)> > m_onIncomingMessageCallbacks;
    std::map<int, std::function<void(int,const boost::system::error_code&)> > m_onErrorCallbacks;

    tcp::socket m_socket;
    tcp::resolver m_resolver;

    std::unique_ptr<RingBuffer<Message> > m_messageQueue;
    std::unique_ptr<RingBuffer<Message> > m_priorityMessageQueue;
    std::unique_ptr<RingBuffer<Streaming::ConstBuffer> > m_sendQHeaders;
    int m_messageBytesSend = 0; // future tense
    int m_messageBytesSent = 0; // past tense

    Streaming::BufferPool m_receiveStream;
    mutable std::atomic<int> m_lastCallbackId;
    std::atomic<bool> m_isClosingDown;
    bool m_firstPacket = true;
    bool m_isConnecting = false;
    bool m_isConnected;
    bool m_sendingInProgress = false;
    bool m_acceptedConnection = false;

    int m_queueSizeMain = 2000; // config setting for the ringbuffers sizes
    int m_priorityQueueSize = 20; // ditto

    short m_reconnectStep = 0;
    boost::asio::deadline_timer m_reconnectDelay;

    // for these I write 'ping' but its 'pong' for server (incoming) connections.
    boost::asio::deadline_timer m_pingTimer;
    boost::asio::deadline_timer m_sendTimer;
    Message m_pingMessage;

    // chunked messages can be recombined.
    Streaming::BufferPool m_chunkedMessageBuffer;
    int m_chunkedServiceId = -1;
    int m_chunkedMessageId = -1;
    std::map<int, int> m_chunkedHeaderData;
};

class NetworkManagerServer
{
public:
    NetworkManagerServer(const std::shared_ptr<NetworkManagerPrivate> &parent, tcp::endpoint endpoint, const std::function<void(NetworkConnection&)> &callback);

    void shutdown();

private:
    void setupCallback();
    void acceptConnection(boost::system::error_code error);

    std::weak_ptr<NetworkManagerPrivate> d;
    tcp::acceptor m_acceptor;
    tcp::socket m_socket;
    std::function<void(NetworkConnection&)> onIncomingConnection; // callback
};


struct BannedNode
{
    EndPoint endPoint;
    boost::posix_time::ptime banTimeout;
};

class NetworkManagerPrivate
{
public:
    NetworkManagerPrivate(boost::asio::io_service &service);
    ~NetworkManagerPrivate();

    inline void alwaysConnectingNewConnectionHandler(NetworkConnection &con) {
        con.accept();
    }

    void punishNode(int connectionId, int punishScore);
    void cronHourly(const boost::system::error_code& error);

    boost::asio::io_service& ioService;

    std::map<int, std::shared_ptr<NetworkManagerConnection> > connections;
    std::deque<std::shared_ptr<NetworkManagerConnection> > unusedConnections;
    int lastConnectionId;

    std::recursive_mutex mutex; // to lock access to things like the connections map
    std::mutex connectionMutex;
    bool isClosingDown;

    std::vector<NetworkManagerServer *> servers;

    std::list<BannedNode> banned;
    std::list<NetworkServiceBase*> services;
    boost::asio::deadline_timer m_cronHourly;

    // support for the p2p legacy envelope design
    uint8_t networkId[4] = { 0xE3, 0xE1, 0xF3, 0xE8};
    std::map<int, std::string> messageIds;
    std::map<std::string, int> messageIdsReverse;
};

#endif
