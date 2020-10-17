/*
 * This file is part of the Flowee project
 * Copyright (C) 2020 Tom Zander <tomz@freedommail.ch>
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
#ifndef DATALISTENERINTERFACE_H
#define DATALISTENERINTERFACE_H

#include <deque>

class Tx;
class BlockHeader;

class DataListenerInterface
{
public:
    DataListenerInterface();
    virtual ~DataListenerInterface();

    /**
     * @brief newTransactions announces a list of transactions pushed to us from a peer.
     * @param header the block header these transactions appeared in.
     * @param blockHeight the blockheight we know the header under.
     * @param blockTransactions The actual transactions.
     */
    virtual void newTransactions(const BlockHeader &header, int blockHeight, const std::deque<Tx> &blockTransactions) = 0;
    /// A single transaction that matches our filters, forwarded to us as it hits a mempool.
    virtual void newTransaction(const Tx &tx) = 0;
    /// notify when we get a newer (higher) blockheight
    virtual void setLastSynchedBlockHeight(int height);
};

#endif
