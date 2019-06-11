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
#include "SpentOuputIndexer.h"

SpentOuputIndexer::SpentOuputIndexer(boost::asio::io_service &service, const boost::filesystem::path &basedir)
    : m_txdb(service, basedir)
{
}

int SpentOuputIndexer::blockheight() const
{
    return m_txdb.blockheight();
}

uint256 SpentOuputIndexer::blockId() const
{
    return m_txdb.blockId();
}

void SpentOuputIndexer::blockFinished(int blockheight, const uint256 &blockId)
{
    m_txdb.blockFinished(blockheight, blockId);
}

void SpentOuputIndexer::insertSpentTransaction(const uint256 &prevTxId, int prevOutIndex, int blockHeight, int offsetInBlock)
{
    m_txdb.insert(prevTxId, prevOutIndex, blockHeight, offsetInBlock);
}

void SpentOuputIndexer::saveCaches()
{
    m_txdb.saveCaches();
}

SpentOuputIndexer::TxData SpentOuputIndexer::findSpendingTx(const uint256 &txid, int output) const
{
    TxData answer;
    auto item = m_txdb.find(txid, output);
    if (item.isValid()) {
        answer.blockHeight = item.blockHeight();
        answer.offsetInBlock = item.offsetInBlock();
    }
    return answer;
}
