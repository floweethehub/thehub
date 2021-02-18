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
class CTransaction;
class uint256;
class Tx;
class FastBlock;
class DoubleSpendProof;

class ValidationInterface {
public:
    /** Notifies listeners of updated transaction data, and optionally the block it is found in. */
    virtual void syncTransaction(const CTransaction &tx) {}

    /** Notifies listeners of updated transaction data, and optionally the block it is found in. */
    virtual void syncTx(const Tx &) {}

    /** Notifies listeners of updated transaction data, on a new accepted block. */
    virtual void syncAllTransactionsInBlock(const CBlock *pblock) {}

    /** Notifies listeners of updated transaction data, on a new accepted block. */
    virtual void syncAllTransactionsInBlock(const FastBlock &, CBlockIndex *) {}

    /** Notifies listeners of a new active block chain. */
    virtual void setBestChain(const CBlockLocator &locator) {}

    /** Notifies listeners of an updated transaction without new data (for now: a coinbase potentially becoming visible). */
    virtual void updatedTransaction(const uint256 &hash) {}

    /** Notifies listeners about an inventory item being seen on the network. */
    virtual void inventory(const uint256 &hash) {}

    /** Tells listeners to broadcast their data. */
    virtual void resendWalletTransactions(int64_t nBestBlockTime) {}

    /**
     * Notifies listeners that we received a double-spend.
     * First is the tx that is in our mempool, duplicate is the one we received and reject
     */
    virtual void doubleSpendFound(const Tx &first, const Tx &duplicate) {}

    /**
     * Notifies listeners that we received a double-spend proof.
     * First is the tx that is in our mempool, proof is the actual proof.
     */
    virtual void doubleSpendFound(const Tx &txInMempool, const DoubleSpendProof &proof) {}
};

class ValidationInterfaceBroadcaster : public ValidationInterface
{
public:
    void syncTransaction(const CTransaction &tx) override;
    void syncTx(const Tx &) override;
    void syncAllTransactionsInBlock(const CBlock *pblock) override;
    void syncAllTransactionsInBlock(const FastBlock &block, CBlockIndex *index) override;
    void setBestChain(const CBlockLocator &locator) override;
    void updatedTransaction(const uint256 &hash) override;
    void inventory(const uint256 &hash) override;
    void resendWalletTransactions(int64_t nBestBlockTime) override;
    void doubleSpendFound(const Tx &first, const Tx &duplicate) override;
    void doubleSpendFound(const Tx &txInMempool, const DoubleSpendProof &proof) override;

    void addListener(ValidationInterface *impl);
    void removeListener(ValidationInterface *impl);
    void removeAll();

private:
    std::list<ValidationInterface*> m_listeners;
};

ValidationInterfaceBroadcaster &ValidationNotifier();

#endif
