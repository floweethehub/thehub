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
#ifndef SPENTOUPUTINDEXER_H
#define SPENTOUPUTINDEXER_H

#include <UnspentOutputDatabase.h>

class SpentOuputIndexer
{
public:
    SpentOuputIndexer(boost::asio::io_service &service, const boost::filesystem::path &basedir);

    int blockheight() const;
    uint256 blockId() const;
    void blockFinished(int blockheight, const uint256 &blockId);

    /**
     * Insert transaction that spent an output.
     * Inputs spent old transaction's outputs. To find those spending a certain
     * output we insert them here when they get spend.
     */
    void insertSpentTransaction(const uint256 &prevTxId, int prevOutIndex, int blockHeight, int offsetInBlock);

    void saveCaches();

    /// The transaction that spent an output.
    struct TxData {
        int blockHeight = -1;
        int offsetInBlock = 0;
    };

    TxData findSpendingTx(const uint256 &txid, int output) const;

private:
    UnspentOutputDatabase m_txdb;
};

#endif