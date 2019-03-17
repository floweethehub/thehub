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
#include "NetworkSubscriptionService.h"
#include "NetworkManager.h"

NetworkSubscriptionService::NetworkSubscriptionService(int serviceId)
    : NetworkService(serviceId)
{
}

NetworkSubscriptionService::~NetworkSubscriptionService()
{
    for (auto r : m_remotes) {
        delete r;
    }
}

void NetworkSubscriptionService::onIncomingMessage(const Message &message, const EndPoint &ep)
{
    for (auto remote : m_remotes) {
        if (remote->connection.endPoint().connectionId == ep.connectionId) {
            handle(remote, message, ep);
            return;
        }
    }
    for (auto remote : m_remotes) {
        if (remote->connection.endPoint().announcePort == ep.announcePort && remote->connection.endPoint().hostname == ep.hostname) {
            handle(remote, message, ep);
            return;
        }
    }
    NetworkConnection con = manager()->connection(ep, NetworkManager::OnlyExisting);
    if (!con.isValid())
        return;
    con.setOnDisconnected(std::bind(&NetworkSubscriptionService::onDisconnected, this, std::placeholders::_1));
    Remote *r = createRemote();
    r->connection = std::move(con);
    m_remotes.push_back(r);
    handle(r, message, ep);
}

void NetworkSubscriptionService::onDisconnected(const EndPoint &endPoint)
{
    for (auto iter = m_remotes.begin(); iter != m_remotes.end(); ++iter) {
        if ((*iter)->connection.endPoint().connectionId == endPoint.connectionId) {
            delete (*iter);
            m_remotes.erase(iter);
            return;
        }
    }
}

NetworkSubscriptionService::Remote::~Remote()
{
}
