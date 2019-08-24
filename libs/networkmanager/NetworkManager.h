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
#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <string>
#include <cstdint>
#include <vector>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "NetworkConnection.h"

class NetworkManagerPrivate;
class NetworkServiceBase;

/**
 * The NetworkManager is the main entry-point of this library.
 *
 * Creating a NetworkManager allows you to manage your connections and their message-flows.
 */
class NetworkManager
{
public:
    NetworkManager(boost::asio::io_service &service);
    ~NetworkManager();

    enum ConnectionEnum {
        AutoCreate,
        OnlyExisting    ///< If no existing connection is found, an invalid one is returned.
    };

    /**
     * Find a connection based on explicit data from the remote argument.
     * @param remote the datastructure with all the details of a remote used in the connection.
     *      The announcePort and the ipAddress are required to be filled.
     * @param connect Indicate what to do when the connection doesn't exist yet.
     */
    NetworkConnection connection(const EndPoint &remote, ConnectionEnum connect = AutoCreate);

    EndPoint endPoint(int remoteId) const;

    /**
     * Punish a node that misbehaves (for instance if it breaks your protocol).
     * A node that gathers a total of 1000 points is banned for 24 hours,
     * every hour 100 points are subtracted from a each node's punishment-score.
     * @see Message.remote
     * @see NetworkManager::punishNode()
     */
    void punishNode(int remoteId, int punishment);

    /**
     * Listen for incoming connections.
     * Adds a callback that will be called when a new connection comes in.
     *
     * New connections can be vetted in this callback and you need to call NetworkConnection::accept() on
     * the new connection in your callback handler method.
     */
    void bind(const boost::asio::ip::tcp::endpoint &endpoint, const std::function<void(NetworkConnection&)> &callback);

    /**
     * Listen for incoming connections.
     * Adds a callback that will be called when a new connection comes in.
     *
     * New connections are always accepted.
     * the new connection in your callback handler method.
     */
    void bind(const boost::asio::ip::tcp::endpoint &endpoint);

    void addService(NetworkServiceBase *service);
    void removeService(NetworkServiceBase *service);

    std::weak_ptr<NetworkManagerPrivate> priv(); ///< \internal

private:
    std::shared_ptr<NetworkManagerPrivate> d;
};

#include <Logger.h>

inline Log::Item operator<<(Log::Item item, const boost::asio::ip::tcp::endpoint &ep) {
    if (item.isEnabled()) {
        const bool old = item.useSpace();
        item.nospace() << '[' << ep.address().to_string() << ":" << ep.port() << "]";
        if (old)
            return item.space();
    }
    return item;
}
template<class V>
inline Log::SilentItem operator<<(Log::SilentItem item, const boost::asio::ip::tcp::endpoint&) { return item; }

#endif
