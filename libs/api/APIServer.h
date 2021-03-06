/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2021 Tom Zander <tom@flowee.org>
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
#ifndef APISERVER_H
#define APISERVER_H

#include "NetProtect.h"

#include <streaming/BufferPool.h>
#include <networkmanager/NetworkManager.h>
#include <networkmanager/NetworkService.h>

#include <univalue.h>
#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <list>
#include <boost/asio/deadline_timer.hpp>
#include <boost/thread.hpp>

namespace Api {

class Parser;
class ASyncParser;
class SessionData;

class Server {
public:
    Server(boost::asio::io_service &service);
    ~Server();

    void addService(NetworkService *service);

    // return a pool for the current thread;
    Streaming::BufferPool &pool(int reserveSize) const;

    NetworkConnection copyConnection(const NetworkConnection &orig);

    /// create a message stating that we had a failure.
    Message createFailedMessage(const Message &origin, const std::string &failReason) const;

private:
    void newConnection(NetworkConnection &connection);
    void connectionRemoved(const EndPoint &endPoint);
    void incomingMessage(const Message &message);

    void checkConnections(boost::system::error_code error);

    class Connection {
    public:
        Connection(Server *parent, NetworkConnection && connection);
        ~Connection();
        void incomingMessage(const Message &message);

        NetworkConnection m_connection;

    private:
        /// Handle a parser that just calls the old RPC code
        void handleRpcParser(const std::unique_ptr<Parser> &parser, const Message &message);
        /// Handle a parser that does the heavy lifting itself.
        void handleMainParser(const std::unique_ptr<Parser> &parser, const Message &message);
        /// Handles an async parser, asynchroniously.
        void startASyncParser(std::unique_ptr<Parser> && parser);

        void sendFailedMessage(const Message &origin, const std::string &failReason);

        Server * const m_parent;
        std::map<uint32_t, SessionData*> m_properties;
        std::array<std::atomic_bool, 10> m_runningParsers;
    };

    struct NewConnection {
        NetworkConnection connection;
        uint32_t initialConnectionTime;
    };

    NetworkManager m_networkManager;
    NetProtect m_netProtect;

    mutable std::mutex m_mutex; // protects the next 4 vars.
    std::list<Connection*> m_connections; // make this a list of shared_ptrs to Connection
    std::list<NewConnection> m_newConnections;
    bool m_timerRunning;
    boost::asio::deadline_timer m_newConnectionTimeout;
};
}

#endif
