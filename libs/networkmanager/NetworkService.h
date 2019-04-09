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
#ifndef NETWORKSERVICE_H
#define NETWORKSERVICE_H

#include "NetworkConnection.h"
#include "NetworkServiceBase.h"

#include <vector>

#include <streaming/BufferPool.h>

/**
 * Implement a handler of service messages.
 *
 * In the NetworkManager system messages are optionally routed to handlers
 * using a service ID. Handling messages for a specific service, as they come
 * from the network can be done by inherting from the NetworkService class
 * and reimplementing the onIncomiingMessage() call.
 *
 * Please note that this class adds to the very basic NetworkServiceBase class
 * a safe way to reply to your incoming messages.
 */
class NetworkService : public NetworkServiceBase
{
public:
    ~NetworkService();

    void onIncomingMessage(const Message &message, const EndPoint &ep) override;

    class Remote {
    public:
        Remote() {}
        Remote(int poolSize) : pool(poolSize) {}
        virtual ~Remote();
        NetworkConnection connection;
        Streaming::BufferPool pool;
    };
    /// pure virtual handler method that attaches a remote
    virtual void onIncomingMessage(Remote *con, const Message &message, const EndPoint &ep) = 0;

protected:
    /// constructor
    NetworkService(int serviceId);

    /// factory method, please return a subclass of Remote with your own data in it
    virtual Remote *createRemote();

    std::vector<Remote*> m_remotes;

private:
    void onDisconnected(const EndPoint &endPoint);
};

#endif
