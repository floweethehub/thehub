/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2015 The Bitcoin Core developers
 * Copyright (C) 2017-2019 Tom Zander <tomz@freedommail.ch>
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

#ifndef FLOWEE_VALIDATIONINTERFACE_H
#define FLOWEE_VALIDATIONINTERFACE_H

#include <boost/shared_ptr.hpp>
#include <list>

class CBlock;
struct CBlockLocator;
class CBlockIndex;
class CReserveScript;
class CTransaction;
class uint256;
class Tx;
class FastBlock;
class DoubleSpendProof;

class ValidationInterface {
public:
    /** Notifies listeners of updated transaction data, and optionally the block it is found in. */
    virtual void SyncTransaction(const CTransaction &tx) {}

    /** Notifies listeners of updated transaction data, and optionally the block it is found in. */
    virtual void SyncTx(const Tx &) {}

    /** Notifies listeners of updated transaction data, on a new accepted block. */
    virtual void SyncAllTransactionsInBlock(const CBlock *pblock) {}

    /** Notifies listeners of updated transaction data, on a new accepted block. */
    virtual void SyncAllTransactionsInBlock(const FastBlock &, CBlockIndex *) {}

    /** Notifies listeners of a new active block chain. */
    virtual void SetBestChain(const CBlockLocator &locator) {}

    /** Notifies listeners of an updated transaction without new data (for now: a coinbase potentially becoming visible). */
    virtual void UpdatedTransaction(const uint256 &hash) {}

    /** Notifies listeners about an inventory item being seen on the network. */
    virtual void Inventory(const uint256 &hash) {}

    /** Tells listeners to broadcast their data. */
    virtual void ResendWalletTransactions(int64_t nBestBlockTime) {}

    /** Notifies listeners that a key for mining is required (coinbase) */
    virtual void GetScriptForMining(boost::shared_ptr<CReserveScript>) {}

    /**
     * Notifies listeners that we received a double-spend.
     * First is the tx that is in our mempool, duplicate is the one we received and reject
     */
    virtual void DoubleSpendFound(const Tx &first, const Tx &duplicate) {}

    /**
     * Notifies listeners that we received a double-spend proof.
     * First is the tx that is in our mempool, proof is the actual proof.
     */
    virtual void DoubleSpendFound(const Tx &txInMempool, const DoubleSpendProof &proof) {}
};

class ValidationInterfaceBroadcaster : public ValidationInterface
{
public:
    void SyncTransaction(const CTransaction &tx) override;
    void SyncTx(const Tx &) override;
    void SyncAllTransactionsInBlock(const CBlock *pblock) override;
    void SyncAllTransactionsInBlock(const FastBlock &block, CBlockIndex *index) override;
    void SetBestChain(const CBlockLocator &locator) override;
    void UpdatedTransaction(const uint256 &hash) override;
    void Inventory(const uint256 &hash) override;
    void ResendWalletTransactions(int64_t nBestBlockTime) override;
    void GetScriptForMining(boost::shared_ptr<CReserveScript>) override;
    void DoubleSpendFound(const Tx &first, const Tx &duplicate) override;
    void DoubleSpendFound(const Tx &txInMempool, const DoubleSpendProof &proof) override;

    void addListener(ValidationInterface *impl);
    void removeListener(ValidationInterface *impl);
    void removeAll();

private:
    std::list<ValidationInterface*> m_listeners;
};

ValidationInterfaceBroadcaster &ValidationNotifier();

#endif
