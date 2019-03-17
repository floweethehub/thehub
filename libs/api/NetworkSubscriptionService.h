/*
 * This file is part of the Flowee project * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
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
#ifndef NETWORKSUBSCRIPTIONSERVICE_H
#define NETWORKSUBSCRIPTIONSERVICE_H

#include "NetworkConnection.h"
#include "NetworkService.h"

#include <vector>

class NetworkSubscriptionService : public NetworkService
{
public:
    ~NetworkSubscriptionService();

    /// implemented NetworkService method
    void onIncomingMessage(const Message &message, const EndPoint &ep) override;

protected:
    /// constructor
    NetworkSubscriptionService(int serviceId);

    class Remote {
    public:
        virtual ~Remote();
        NetworkConnection connection;
    };
    /// factory method, please return a subclass of Remote with your own data in it
    virtual Remote *createRemote() = 0;
    /// pure virtual handler method that attaches a remote
    virtual void handle(Remote *con, const Message &message, const EndPoint &ep) = 0;

    std::vector<Remote*> m_remotes;

private:
    void onDisconnected(const EndPoint &endPoint);
};

#endif
