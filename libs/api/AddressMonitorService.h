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
#ifndef ADDRESSMONITORSERVICE_H
#define ADDRESSMONITORSERVICE_H

#include <primitives/pubkey.h>

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
    void SyncAllTransactionsInBlock(const FastBlock &block, CBlockIndex *index) override;
    // void SetBestChain(const CBlockLocator &locator) override;
    void DoubleSpendFound(const Tx &first, const Tx &duplicate) override;

    void onIncomingMessage(Remote *con, const Message &message, const EndPoint &ep) override;

    inline void setMempool(CTxMemPool *mempool) {
        m_mempool = mempool;
    }

protected:
    class RemoteWithKeys : public Remote {
    public:
        std::set<CKeyID> keys;
    };

    // NetworkService interface
    Remote *createRemote() override {
        return new RemoteWithKeys();
    }

private:
    enum FindReason {
        Mempool,
        Confirmed,
        Conflicted
    };

    void findTransactions(Tx::Iterator && iter, FindReason findReason, const FastBlock *block = nullptr);
    void updateBools();
    void findTxInMempool(int connectionId, const CKeyID &keyId);

    Streaming::BufferPool m_pool;

    // true if any remote added a watch
    bool m_findP2PKH = false;

    CTxMemPool *m_mempool = nullptr;
};

#endif
