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
#include "TxIndexer.h"

TxIndexer::TxIndexer(boost::asio::io_service &service, const boost::filesystem::path &basedir)
    : m_txdb(service, basedir)
{
}

int TxIndexer::blockheight() const
{
    return m_txdb.blockheight();
}

uint256 TxIndexer::blockId() const
{
    return m_txdb.blockId();
}

void TxIndexer::blockFinished(int blockheight, const uint256 &blockId)
{
    m_txdb.blockFinished(blockheight, blockId);
}

void TxIndexer::insert(const uint256 &txid, int blockHeight, int offsetInBlock)
{
    m_txdb.insert(txid, 0, blockHeight, offsetInBlock);
}
