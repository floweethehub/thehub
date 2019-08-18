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

#include <QString>

#include <Message.h>
#include <NetworkManager.h>
#include <NetworkService.h>
#include <QMutex>
#include <QWaitCondition>
#include <WorkerThreads.h>
#include <qtimer.h>

class AddressIndexer;
class TxIndexer;
class SpentOutputIndexer;

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

    /// load config, if prioHubLocation is valid prefer that one.
    void loadConfig(const QString &filename, const EndPoint &prioHubLocation);

    // network service API
    void onIncomingMessage(Remote *con, const Message &message, const EndPoint &ep) override;

    /// called by the workerthreads to get a block-message. Blocking.
    /// \param height is the requested blockheight of the next block to process
    Message nextBlock(int height, unsigned long timeout = ULONG_MAX);

private slots:
    void requestBlock(int newBlockHeight = -1);
    void checkBlockArrived();

    void onFindAddressRequest(const Message &message);

signals:
    void requestFindAddress(const Message &message);

private:
    void hubConnected(const EndPoint &ep);
    void hubDisconnected();
    void hubSentMessage(const Message &message);

    void clientConnected(NetworkConnection &con);

private:
    QTimer m_pollingTimer;
    Streaming::BufferPool m_pool;
    Streaming::BufferPool m_poolAddressAnswers;

    boost::filesystem::path m_basedir;
    TxIndexer *m_txdb = nullptr;
    SpentOutputIndexer *m_spentOutputDb = nullptr;
    AddressIndexer *m_addressdb = nullptr;

    WorkerThreads m_workers;
    NetworkManager m_network;
    NetworkConnection m_serverConnection;

    bool m_indexingFinished = false;
    bool m_isServer = false; /// remembers if we (successfully) called m_network::bind() once.

    int m_lastRequestedBlock = 0;
    quint64 m_timeLastRequest = 0;
    quint64 m_timeLastLogLine = 0;



    // data to process blocks in different workers.
    Message m_nextBlock;
    mutable QMutex m_nextBlockLock;
    mutable QWaitCondition m_waitForBlock;
};

#endif
