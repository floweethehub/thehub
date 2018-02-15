/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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

class NetworkManager;
class Message;
class EndPoint;

class NetworkService
{
public:
    ~NetworkService();
    inline int id() const {
        return m_id;
    }

    virtual void onIncomingMessage(const Message &message, const EndPoint &ep) = 0;

    NetworkManager *manager() const;
    void setManager(NetworkManager *manager);

protected:
    NetworkService(int id);

    const int m_id;
    NetworkManager *m_manager;
};

#endif
