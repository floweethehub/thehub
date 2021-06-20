/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tom@flowee.org>
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
#ifndef NETWORKSERVICEBASE_H
#define NETWORKSERVICEBASE_H

class NetworkManager;
class Message;
class EndPoint;

/**
 * Base class for the handling of incoming messages filtered to a service.
 *
 * In the NetworkManager system, messages are sent to services, as identified
 * by the Service-id (a field in the Message class).
 *
 * The suggested way to implement client server communication is that the client
 * sends messages to one (or more) services identified by the service-id. On the
 * server side the Network Manager will find an implementation of NetworkServiceBase
 * registered with the relevant service-id and this will then get the messages
 * delivered.
 *
 * If your service wants to safely reply to those messages it is adviced you
 * use the NetworkService baseclass instead.
 */
class NetworkServiceBase
{
public:
    virtual ~NetworkServiceBase();
    inline int id() const {
        return m_id;
    }

    virtual void onIncomingMessage(const Message &message, const EndPoint &ep) = 0;

    NetworkManager *manager() const;

protected:
    friend class NetworkManager;
    void setManager(NetworkManager *manager);
    NetworkServiceBase(int id);

    const int m_id;
    NetworkManager *m_manager;
};

#endif
