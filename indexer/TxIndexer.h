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
#ifndef TXINDEXER_H
#define TXINDEXER_H

#include <UnspentOutputDatabase.h>



class TxIndexer
{
public:
    TxIndexer(boost::asio::io_service &service, const boost::filesystem::path &basedir);

    int blockheight() const;
    uint256 blockId() const;
    void blockFinished(int blockheight, const uint256 &blockId);
    void insert(const uint256 &txid, int blockHeight, int offsetInBlock);

    struct TxData {
        int blockHeight = -1;
        int offsetInBlock = 0;
    };

    TxData find(const uint256 &txid) const;

private:
    UnspentOutputDatabase m_txdb;
};

#endif
