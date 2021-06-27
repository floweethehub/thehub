/*
 * This file is part of the Flowee project
 * Copyright (C) 2021 Tom Zander <tom@flowee.org>
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
#ifndef CONNECTIONAUTHORIZER_H
#define CONNECTIONAUTHORIZER_H

#include "NetworkConnection.h"

#include <boost/asio/io_service.hpp>
#include <boost/asio/deadline_timer.hpp>

/**
 * Helper class to have basic new connection authorization features like login.
 *
 * You can bind to an existing network manager and point the callback in
 * NetworkManager::bind() to the newConnection() method on an instance of this
 * class, or rather a subclass.
 *
 * New connections are checked based on simple accept() parameters first,
 * and when the first message comes in we expect a login type message that will
 * be checked in validateLogin().
 *
 * To reject connections reimplement those methods to return false.
 *
 *  Notice that a connection that never sent a login message will be disconnected
 *  after a couple of seconds too. Naturally any connection that violates the protocol
 *  will get disconnected by the network manager itself.
 */
class ConnectionAuthorizer
{
public:
    ConnectionAuthorizer(boost::asio::io_service &service);
    virtual ~ConnectionAuthorizer();

    void newConnection(NetworkConnection &con);

protected:
    /**
     * Return true if the first message we receive from a connection
     * is to be accepted as a proper login.
     *
     * Default implementation always returns true.
     */
    virtual bool validateLogin(const Message &message);
    /**
     * Return true if a new connection from endpoint is to be accepted.
     *
     * Default implementation always returns true.
     */
    virtual bool accept(const EndPoint &ep);

private:
    void onIncomingMessage(const Message &message);

    void checkConnections(boost::system::error_code error);

    struct IncomingConnection {
        IncomingConnection(NetworkConnection &&con);
        NetworkConnection connection;
        uint32_t connectedTime;
    };
    mutable std::mutex m_lock;
    std::vector<IncomingConnection> m_openConnections;
    bool m_timerRunning;
    boost::asio::deadline_timer m_newConTimer;
};

#endif
