/*
 * This file is part of the Flowee project
 * Copyright (C) 2016,2019-2020 Tom Zander <tomz@freedommail.ch>
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
#ifndef TXVULCANO_H
#define TXVULCANO_H

#include <QString>

#include "Wallet.h"

#include <streaming/BufferPool.h>
#include <networkmanager/NetworkManager.h>
#include <networkmanager/NetworkConnection.h>

#include <boost/asio/io_service.hpp>
#include <primitives/FastTransaction.h>

#include <boost/asio/deadline_timer.hpp>
#include <qlist.h>
#include <qmutex.h>
#include <qthread.h>
#include <vector>

namespace Streaming {
    class MessageBuilder;
}

class TxVulcano : public QObject
{
    Q_OBJECT
public:
    TxVulcano(boost::asio::io_service &ioService, const QString &walletname);
    ~TxVulcano();

    void tryConnect(const EndPoint &ep);

    void setMaxBlockSize(int sizeInMb);
    inline void setMaxNumTransactions(int num) {
        assert(num > 0);
        m_transactionsToCreate = num;
    }

    /**
     * A setup where the TxVulcano owns the addresses
     * means we can ask the Hub to mine some blocks with addresses we create
     * as coinbase. This implies we run on regtest.
     */
    void setAddressesAreOwned(bool yes);

    bool canRunGenerate() const;
    void setCanRunGenerate(bool canRunGenerate);

    bool addPrivKey(const QString &key);

signals:
    void newBlockFound(const Message &message);

private slots:
    // handle incoming GetBlockReply messages
    void processNewBlock(const Message &message);
    void createTransactions_priv();

private:
    void connectionEstablished(const EndPoint &ep);
    void disconnected();
    void incomingMessage(const Message &message);
    // requires m_walletMutex to be locked by caller
    void requestNextBlocksChunk();

    void createTransactions(const boost::system::error_code& error);
    std::vector<char> createOutScript(const std::vector<char> &address);
    void buildGetBlockRequest(Streaming::MessageBuilder &builder, bool &first) const;

    void nowCurrent(); // called when the client has seen all blocks the upstread knows about

    void generate(int blockCount = 1); // generate a block;

    NetworkManager m_networkManager;
    NetworkConnection m_connection;
    bool m_serverSupportsAsync = false;

    Streaming::BufferPool m_Txpool;
    Streaming::BufferPool m_pool;

    // limits
    int m_transactionsToCreate;
    int m_transactionsCreated;
    int m_blockSizeLeft;
    int m_lastPrintedBlockSizeLeft = 0;
    QList<int> m_nextBlockSize;

    boost::asio::deadline_timer m_timer;

    struct UnvalidatedTransaction {
        Tx transaction;
        int unconfirmedDepth = 0;
        std::vector<int> pubKeys;
    };

    QMutex m_miscMutex;
    std::map<int, UnvalidatedTransaction> m_transactionsInProgress;
    int m_lastId = 0;
    bool m_canRunGenerate = false; // i.e. we run on regtest where mining is an API command.

    QMutex m_walletMutex;
    Wallet m_wallet;
    int m_lastSeenBlock = -1;
    int m_highestBlock = -1; // the block that we learned that the remote has.
    bool m_outOfCoin = false; // set to true if there are no unspent coins

    QThread m_workerThread;
};

#endif
