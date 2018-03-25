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
#ifndef ADDRESSMONITORSERVICE_H
#define ADDRESSMONITORSERVICE_H

#include "pubkey.h"

#include <validationinterface.h>
#include <NetworkService.h>
#include <NetworkConnection.h>
#include <primitives/FastTransaction.h>
#include <script/standard.h>

#include <set>

class CTxMemPool;

class AddressMonitorService : public ValidationInterface, public NetworkService
{
public:
    AddressMonitorService();
    ~AddressMonitorService();

    // the hub pushed a transaction into its mempool
    void SyncTx(const Tx &tx) override;
    void SyncAllTransactionsInBlock(const FastBlock &block) override;
    // void SetBestChain(const CBlockLocator &locator) override;
    void DoubleSpendFound(const Tx &first, const Tx &duplicate) override;

    void onIncomingMessage(const Message &message, const EndPoint &ep) override;

    inline void setMempool(CTxMemPool *mempool) {
        m_mempool = mempool;
    }

private:
    struct Remote {
        std::set<CKeyID> keys;
        NetworkConnection connection;
    };

    enum FindReason {
        Mempool,
        Confirmed,
        Conflicted
    };

    void onDisconnected(const EndPoint &endPoint);

    void handle(Remote *con, const Message &message, const EndPoint &ep);

    void findTransactions(Tx::Iterator && iter, FindReason findReason);

    void updateBools();

    void findTxInMempool(int connectionId, const CKeyID &keyId);

    std::vector<Remote*> m_remotes;
    Streaming::BufferPool m_pool;

    // true if any remote added a watch
    bool m_findP2PKH = false;

    CTxMemPool *m_mempool = nullptr;
};

#endif
