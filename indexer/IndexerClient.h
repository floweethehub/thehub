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

#include <QString>
#include <networkmanager/NetworkManager.h>
#include <WorkerThreads.h>
#include <uint256.h>

class IndexerClient
{
public:
    IndexerClient();

    void resolve(const QString &lookup);

    void tryConnectIndexer(const EndPoint &ep);
    void tryConnectHub(const EndPoint &ep);

private:
    void hubConnected(const EndPoint &ep);
    void hubDisconnected();
    void onIncomingHubMessage(const Message &message);

    void indexerConnected(const EndPoint &ep);
    void indexerDisconnected();
    void onIncomingIndexerMessage(const Message &message);

    WorkerThreads m_workers;

    NetworkManager m_network;
    NetworkConnection m_indexConnection;
    NetworkConnection m_hubConnection;
};
