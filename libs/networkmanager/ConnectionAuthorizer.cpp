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
#include "ConnectionAuthorizer.h"

#include <streaming/MessageParser.h>

constexpr int INTRODUCTION_TIMEOUT = 4; // in seconds

ConnectionAuthorizer::ConnectionAuthorizer(boost::asio::io_service &service)
    : m_timerRunning(false),
      m_newConTimer(service)
{
}

ConnectionAuthorizer::~ConnectionAuthorizer()
{
}

void ConnectionAuthorizer::newConnection(NetworkConnection &con)
{
    if (!accept(con.endPoint())) {
        logWarning() << "Rejected incoming connection based on endpoint.";
        return;
    }
    con.setOnIncomingMessage(std::bind(&ConnectionAuthorizer::onIncomingMessage, this, std::placeholders::_1));
    { // lock-scope
        std::unique_lock<std::mutex> lock(m_lock);
        m_openConnections.push_back(IncomingConnection(std::move(con)));

        if (!m_timerRunning) {
            m_timerRunning = true;
            m_newConTimer.expires_from_now(boost::posix_time::seconds(INTRODUCTION_TIMEOUT));
            m_newConTimer.async_wait(std::bind(&ConnectionAuthorizer::checkConnections, this, std::placeholders::_1));
        }
    } // lock-scope
    m_openConnections.back().connection.accept(NetworkConnection::AcceptForLogin);
}

bool ConnectionAuthorizer::validateLogin(const Message&)
{
    return true;
}

bool ConnectionAuthorizer::accept(const EndPoint&)
{
    return true;
}

void ConnectionAuthorizer::onIncomingMessage(const Message &message)
{
    std::unique_lock<std::mutex> lock(m_lock);
    auto iter = m_openConnections.begin();
    while (iter != m_openConnections.end()) {
        if (iter->connection.connectionId() == message.remote) {
            if (!validateLogin(message)) {
                logWarning() << "Rejected connection due to login failure, disconnecting peer:" << message.remote;
                iter->connection.disconnect();
            }
            m_openConnections.erase(iter);
            return;
        }
    }
}

void ConnectionAuthorizer::checkConnections(boost::system::error_code error)
{
    if (error.value() == boost::asio::error::operation_aborted)
         return;
     std::unique_lock<std::mutex> lock(m_lock);
     const auto limit = time(nullptr) - INTRODUCTION_TIMEOUT; // everything created before limit is too old
     auto iter = m_openConnections.begin();
     while (iter != m_openConnections.end()) {
         if (iter->connectedTime <= limit) {
             logWarning() << "Login-timeout, disconnecting peer:" << iter->connection.connectionId();
             iter->connection.disconnect();
             iter = m_openConnections.erase(iter);
         } else {
             ++iter;
         }
     }

     // restart timer if there is still something left.
     if (!m_openConnections.empty()) {
         m_timerRunning = true;
         m_newConTimer.expires_from_now(boost::posix_time::seconds(2));
         m_newConTimer.async_wait(std::bind(&ConnectionAuthorizer::checkConnections, this, std::placeholders::_1));
     } else {
         m_timerRunning = false;
     }
}


// ---------------------------------------------------------------

ConnectionAuthorizer::IncomingConnection::IncomingConnection(NetworkConnection && con)
    : connection(std::move(con))
{
    connectedTime = time(nullptr);
}
