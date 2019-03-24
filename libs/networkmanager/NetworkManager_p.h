/*
 * This file is part of the Flowee project
 * Copyright (C) 2016, 2019 Tom Zander <tomz@freedommail.ch>
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
#include <Message.h>
#include <streaming/BufferPool.h>

#include <boost/thread/recursive_mutex.hpp>
#include <boost/asio.hpp>
#include <list>
#include <boost/date_time/posix_time/ptime.hpp>
#include <atomic>
#include <interfaces/boost_compat.h>

class NetworkConnection;
class NetworkService;

using boost::asio::ip::tcp;

template<class V>
class RingBuffer
{
public:
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
    enum BufferSize {
        NumItems = 1000
    };

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
    V m_array[NumItems];
    int m_first = 0;
    int m_readIndex = 0;
    int m_next = 0; // last plus one
};

class NetworkManagerConnection
{
public:
    NetworkManagerConnection(const std::shared_ptr<NetworkManagerPrivate> &parent, tcp::socket socket, int connectionId);
    NetworkManagerConnection(const std::shared_ptr<NetworkManagerPrivate> &parent, const EndPoint &remote);
    /// Connects to remote (async)
    void connect();

    int nextCallbackId();

    /// unregister a NetworkConnection. Calls have to be from the strand.
    void removeAllCallbacksFor(int id);

    void queueMessage(const Message &message, NetworkConnection::MessagePriority priority);

    inline bool isConnected() const {
        return m_socket.is_open();
    }

    inline const EndPoint &endPoint() const {
        return m_remote;
    }

    /// add callback, calls have to be on the strand.
    void addOnConnectedCallback(int id, const std::function<void(const EndPoint&)> &callback);
    /// add callback, calls have to be on the strand.
    void addOnDisconnectedCallback(int id, const std::function<void(const EndPoint&)> &callback);
    /// add callback, calls have to be on the strand.
    void addOnIncomingMessageCallback(int id, const std::function<void(const Message&)> &callback);

    /// forcably shut down the connection, soon you should no longer reference this object
    void shutdown(std::shared_ptr<NetworkManagerConnection> me);

    /// only incoming connections need accepting.
    void accept();

    inline void disconnect() {
        close(false);
    }

    BoostCompatStrand m_strand;

    /// move a call to the thread that the strand represents
    void runOnStrand(const std::function<void()> &function);

    inline bool acceptedConnection() const {
        return m_acceptedConnection;
    }

    void punish(int amount);

    short m_punishment; // aka ban-sore

private:
    EndPoint m_remote;

    void onAddressResolveComplete(const boost::system::error_code& error, tcp::resolver::iterator iterator);
    void onConnectComplete(const boost::system::error_code& error);

    /// call then to start sending messages to remote. Will do noting if we are already sending messages.
    void runMessageQueue();
    void sentSomeBytes(const boost::system::error_code& error, std::size_t bytes_transferred);
    void receivedSomeBytes(const boost::system::error_code& error, std::size_t bytes_transferred);

    bool processPacket(const std::shared_ptr<char> &buffer, const char *data);
    void close(bool reconnect = true); // close down connection
    void connect_priv(); // thread-unsafe version of connect
    void reconnectWithCheck(const boost::system::error_code& error); // called from the m_reconectDelay timer
    void finalShutdown(std::shared_ptr<NetworkManagerConnection>);
    void sendPing(const boost::system::error_code& error);
    void pingTimeout(const boost::system::error_code& error);

    inline bool isOutgoing() const {
        return m_remote.announcePort == m_remote.peerPort;
    }

    Streaming::ConstBuffer createHeader(const Message &message);

    std::shared_ptr<NetworkManagerPrivate> d;

    std::map<int, std::function<void(const EndPoint&)> > m_onConnectedCallbacks;
    std::map<int, std::function<void(const EndPoint&)> > m_onDisConnectedCallbacks;
    std::map<int, std::function<void(const Message&)> > m_onIncomingMessageCallbacks;

    tcp::socket m_socket;
    tcp::resolver m_resolver;

    RingBuffer<Message> m_messageQueue;
    RingBuffer<Message> m_priorityMessageQueue;
    RingBuffer<Streaming::ConstBuffer> m_sendQHeaders;
    int m_messageBytesSend; // future tense
    int m_messageBytesSent; // past tense

    Streaming::BufferPool m_receiveStream;
    Streaming::BufferPool m_sendHelperBuffer;
    mutable std::atomic<int> m_lastCallbackId;
    std::atomic<bool> m_isClosingDown;
    bool m_firstPacket;
    bool m_isConnecting;
    bool m_sendingInProgress;
    bool m_acceptedConnection;

    short m_reconnectStep;
    boost::asio::deadline_timer m_reconnectDelay;

    // for these I write 'ping' but its 'pong' for server (incoming) connections.
    boost::asio::deadline_timer m_pingTimer;
    Message m_pingMessage;

    // chunked messages can be recombined.
    Streaming::BufferPool m_chunkedMessageBuffer;
    int m_chunkedServiceId;
    int m_chunkedMessageId;
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

    void punishNode(int connectionId, int punishScore);
    void cronHourly(const boost::system::error_code& error);

    boost::asio::io_service& ioService;

    std::map<int, std::shared_ptr<NetworkManagerConnection> > connections;
    int lastConnectionId;

    boost::recursive_mutex mutex; // to lock access to things like the connections map
    bool isClosingDown;

    std::vector<NetworkManagerServer *> servers;

    std::string apiCookieFilename; // if non-empty, auto login to the API server on connect.

    std::list<BannedNode> banned;
    std::list<NetworkService*> services;
    boost::asio::deadline_timer m_cronHourly;
};

#endif
