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
#ifndef APISERVER_H
#define APISERVER_H

#include <streaming/BufferPool.h>
#include <networkmanager/NetworkManager.h>
#include <networkmanager/NetworkService.h>

#include <univalue.h>
#include <vector>
#include <string>
#include <list>
#include <boost/thread/mutex.hpp>
#include <boost/asio/deadline_timer.hpp>

namespace Api {

class SessionData
{
public:
    SessionData() {}
    virtual ~SessionData();
};

class Server {
public:
    Server(boost::asio::io_service &service);

    void addService(NetworkService *service);

private:
    void newConnection(NetworkConnection &connection);
    void connectionRemoved(const EndPoint &endPoint);
    void incomingMessage(const Message &message);

    void checkConnections(boost::system::error_code error);

    class Connection {
    public:
        Connection(NetworkConnection && connection);
        ~Connection();
        void incomingMessage(const Message &message);

        NetworkConnection m_connection;

    private:
        void sendFailedMessage(const Message &origin, const std::string &failReason);

        Streaming::BufferPool m_bufferPool;
        std::map<uint32_t, SessionData*> m_properties;
    };

    struct NewConnection {
        NetworkConnection connection;
        boost::posix_time::ptime initialConnectionTime;
    };

    NetworkManager m_networkManager;

    mutable boost::mutex m_mutex; // protects the next 4 vars.
    std::list<Connection*> m_connections;
    std::list<NewConnection> m_newConnections;
    bool m_timerRunning;
    boost::asio::deadline_timer m_newConnectionTimeout;
};
}

#endif
