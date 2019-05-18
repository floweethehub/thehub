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
#include "NetworkService.h"
#include "NetworkManager.h"

#include <deque>
#include <atomic>
#include <utility>

class RemoteContainer {
public:
    std::deque<NetworkService::Remote*> data;

    ~RemoteContainer() {
        for (auto r : data) {
            delete r;
        }
    }
};

NetworkService::NetworkService(int serviceId)
    : NetworkServiceBase(serviceId),
      m_remotes(new RemoteContainer())
{
}

NetworkService::~NetworkService()
{
    RemoteContainer *c;
    while (true) {
        c = m_remotes.exchange(nullptr, std::memory_order_acq_rel);
        if (c)
            break;
        // if 'locked' avoid burning CPU
        struct timespec tim, tim2;
        tim.tv_sec = 0;
        tim.tv_nsec = 200;
        nanosleep(&tim , &tim2);
    }
    delete c;
}

void NetworkService::onIncomingMessage(const Message &message, const EndPoint &ep)
{
    for (auto remote : remotes()) {
        if (remote->connection.endPoint().connectionId == ep.connectionId) {
            onIncomingMessage(remote, message, ep);
            return;
        }
    }
    for (auto remote : remotes()) {
        if (remote->connection.endPoint().announcePort == ep.announcePort
                && remote->connection.endPoint().hostname == ep.hostname) {
            onIncomingMessage(remote, message, ep);
            return;
        }
    }
    NetworkConnection con = manager()->connection(ep, NetworkManager::OnlyExisting);
    if (!con.isValid())
        return;
    con.setOnDisconnected(std::bind(&NetworkService::onDisconnected, this, std::placeholders::_1));
    Remote *r = createRemote();
    r->connection = std::move(con);
    addRemote(r);
    onIncomingMessage(r, message, ep);
}

void NetworkService::onDisconnected(const EndPoint &endPoint)
{
    for (auto remote : remotes()) {
        if (remote->connection.endPoint().connectionId == endPoint.connectionId) {
            removeRemote(remote);
            delete remote;
            return;
        }
    }
}

NetworkService::Remote *NetworkService::createRemote()
{
    return new Remote();
}

std::deque<NetworkService::Remote *> NetworkService::remotes() const
{
    RemoteContainer *c;
    while (true) {
        c = m_remotes.exchange(nullptr, std::memory_order_acq_rel);
        if (c)
            break;
        // if 'locked' avoid burning CPU
        struct timespec tim, tim2;
        tim.tv_sec = 0;
        tim.tv_nsec = 200;
        nanosleep(&tim , &tim2);
    }
    std::deque<NetworkService::Remote *> copy = c->data;
    m_remotes.store(c, std::memory_order_release);
    return copy;
}

void NetworkService::addRemote(NetworkService::Remote *remote)
{
    RemoteContainer *c;
    while (true) {
        c = m_remotes.exchange(nullptr, std::memory_order_acq_rel);
        if (c)
            break;
        // if 'locked' avoid burning CPU
        struct timespec tim, tim2;
        tim.tv_sec = 0;
        tim.tv_nsec = 200;
        nanosleep(&tim , &tim2);
    }
    c->data.push_back(remote);
    m_remotes.store(c, std::memory_order_release);

}

void NetworkService::removeRemote(NetworkService::Remote *remote)
{
    RemoteContainer *c;
    while (true) {
        c = m_remotes.exchange(nullptr, std::memory_order_acq_rel);
        if (c)
            break;
        // if 'locked' avoid burning CPU
        struct timespec tim, tim2;
        tim.tv_sec = 0;
        tim.tv_nsec = 200;
        nanosleep(&tim , &tim2);
    }
    for (auto iter = c->data.begin(); iter != c->data.end(); ++iter) {
        if (*iter == remote) {
            c->data.erase(iter);
            break;
        }
    }
    m_remotes.store(c, std::memory_order_release);

}

NetworkService::Remote::~Remote()
{
}
