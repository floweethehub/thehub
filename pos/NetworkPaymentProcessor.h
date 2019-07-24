/*
 * This file is part of the Flowee project
 * Copyright (C) 2018-2019 Tom Zander <tomz@freedommail.ch>
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

#include <NetworkService.h>
#include <NetworkConnection.h>
#include <streaming/BufferPool.h>

#include <QObject>

class NetworkPaymentProcessor : public QObject
{
    Q_OBJECT
public:
    NetworkPaymentProcessor(NetworkConnection &&connection, QObject *parent = nullptr);

    void onIncomingMessage(const Message &message);
    void addListenAddress(const QString &address);

private:
    void connectionEstablished(const EndPoint &endpoint);

    Streaming::BufferPool m_pool;
    NetworkConnection m_connection;
    QList<std::vector<uint8_t>> m_listenAddresses;
};
