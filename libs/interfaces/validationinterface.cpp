/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2014 The Bitcoin Core developers
 * Copyright (C) 2017-2018 Tom Zander <tomz@freedommail.ch>
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

void ValidationInterfaceBroadcaster::SyncTransaction(const CTransaction &tx)
{
    for (auto i : m_listeners) i->SyncTransaction(tx);
}

void ValidationInterfaceBroadcaster::SyncTx(const Tx &tx)
{
    for (auto i : m_listeners) i->SyncTx(tx);
}

void ValidationInterfaceBroadcaster::SyncAllTransactionsInBlock(const CBlock *pblock)
{
    for (auto i : m_listeners) i->SyncAllTransactionsInBlock(pblock);
}

void ValidationInterfaceBroadcaster::SyncAllTransactionsInBlock(const FastBlock &block)
{
    for (auto i : m_listeners) i->SyncAllTransactionsInBlock(block);
}

void ValidationInterfaceBroadcaster::SetBestChain(const CBlockLocator &locator)
{
    for (auto i : m_listeners) i->SetBestChain(locator);
}

void ValidationInterfaceBroadcaster::UpdatedTransaction(const uint256 &hash)
{
    for (auto i : m_listeners) i->UpdatedTransaction(hash);
}

void ValidationInterfaceBroadcaster::Inventory(const uint256 &hash)
{
    for (auto i : m_listeners) i->Inventory(hash);
}

void ValidationInterfaceBroadcaster::ResendWalletTransactions(int64_t nBestBlockTime)
{
    for (auto i : m_listeners) i->ResendWalletTransactions(nBestBlockTime);
}

void ValidationInterfaceBroadcaster::GetScriptForMining(boost::shared_ptr<CReserveScript> s)
{
    for (auto i : m_listeners) i->GetScriptForMining(s);
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
