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
#ifndef INDEXER_H
#define INDEXER_H

#include "AddressIndexer.h"
#include "TxIndexer.h"

#include <NetworkManager.h>
#include <NetworkService.h>
#include <WorkerThreads.h>

class Indexer : public QObject, public NetworkService
{
    Q_OBJECT
public:
    Indexer(const boost::filesystem::path &basedir);
    ~Indexer();

    /// connect to server
    void tryConnectHub(const EndPoint &ep);

    /// listen to incoming requests
    void bind(boost::asio::ip::tcp::endpoint endpoint);

    void loadConfig(const QString &filename);

    // network service API
    void onIncomingMessage(Remote *con, const Message &message, const EndPoint &ep) override;

private slots:
    void addressDbFinishedProcessingBlock();

private:
    void hubConnected(const EndPoint &ep);
    void hubDisconnected();
    void hubSentMessage(const Message &message);

    void clientConnected();

    void requestBlock();
    void processNewBlock(const Message &message);

private:
    Streaming::BufferPool m_pool;
    WorkerThreads m_workers;
    TxIndexer m_txdb;
    AddressIndexer m_addressdb;
    NetworkManager m_network;
    NetworkConnection m_serverConnection;

    bool m_enableTxDB = true, m_enableAddressDb = false;
};

#endif
