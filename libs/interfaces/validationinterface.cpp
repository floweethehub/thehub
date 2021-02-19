/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2014 The Bitcoin Core developers
 * Copyright (C) 2017-2021 Tom Zander <tom@flowee.org>
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

#include "validationinterface.h"

ValidationInterfaceBroadcaster &ValidationNotifier() {
    static ValidationInterfaceBroadcaster s_instance;
    return s_instance;
}

void ValidationInterfaceBroadcaster::syncTransaction(const CTransaction &tx)
{
    for (auto i : m_listeners) i->syncTransaction(tx);
}

void ValidationInterfaceBroadcaster::syncTx(const Tx &tx)
{
    for (auto i : m_listeners) i->syncTx(tx);
}

void ValidationInterfaceBroadcaster::syncAllTransactionsInBlock(const CBlock *pblock)
{
    for (auto i : m_listeners) i->syncAllTransactionsInBlock(pblock);
}

void ValidationInterfaceBroadcaster::syncAllTransactionsInBlock(const FastBlock &block, CBlockIndex *index)
{
    for (auto i : m_listeners) i->syncAllTransactionsInBlock(block, index);
}

void ValidationInterfaceBroadcaster::setBestChain(const CBlockLocator &locator)
{
    for (auto i : m_listeners) i->setBestChain(locator);
}

void ValidationInterfaceBroadcaster::updatedTransaction(const uint256 &hash)
{
    for (auto i : m_listeners) i->updatedTransaction(hash);
}

void ValidationInterfaceBroadcaster::inventory(const uint256 &hash)
{
    for (auto i : m_listeners) i->inventory(hash);
}

void ValidationInterfaceBroadcaster::resendWalletTransactions(int64_t nBestBlockTime)
{
    for (auto i : m_listeners) i->resendWalletTransactions(nBestBlockTime);
}

void ValidationInterfaceBroadcaster::doubleSpendFound(const Tx &first, const Tx &duplicate)
{
    for (auto i : m_listeners) i->doubleSpendFound(first, duplicate);
}

void ValidationInterfaceBroadcaster::doubleSpendFound(const Tx &txInMempool, const DoubleSpendProof &proof)
{
    for (auto i : m_listeners) i->doubleSpendFound(txInMempool, proof);
}

void ValidationInterfaceBroadcaster::chainReorged(CBlockIndex *oldTip, const std::vector<FastBlock> &revertedBlocks)
{
    for (auto i : m_listeners) i->chainReorged(oldTip, revertedBlocks);
}

void ValidationInterfaceBroadcaster::addListener(ValidationInterface *impl)
{
    m_listeners.push_back(impl);
}

void ValidationInterfaceBroadcaster::removeListener(ValidationInterface *impl)
{
    m_listeners.remove(impl);
}

void ValidationInterfaceBroadcaster::removeAll()
{
    m_listeners.clear();
}
