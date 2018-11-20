/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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
#ifndef UNSPENTOUTPUTDATA_H
#define UNSPENTOUTPUTDATA_H

#include <utxo/UnspentOutputDatabase.h>


class UnspentOutputData
{
public:
    UnspentOutputData(const UnspentOutput &uo);
    UnspentOutputData() = default;

    inline bool isValid() const {
        return m_uo.isValid() && m_txVer >= 0 && m_outputValue >= 0;
    }

    inline uint256 prevTxId() const {
        return m_uo.prevTxId();
    }
    inline int outIndex() const {
        return m_uo.outIndex();
    }
    /// return the offset in the block. In bytes. Notice that offsetInBlock == 81 implies this is a coinbase.
    inline int offsetInBlock() const {
        return m_uo.offsetInBlock();
    }
    inline int blockHeight() const {
        return m_uo.blockHeight();
    }

    inline bool isCoinbase() const {
        return m_uo.isCoinbase();
    }

    inline Streaming::ConstBuffer data() const {
        return m_uo.data();
    }

    inline int prevTxVersion() const {
        return m_txVer;
    }
    inline std::int64_t outputValue() const {
        return m_outputValue;
    }

    inline Streaming::ConstBuffer outputScript() const {
        return m_outputScript;
    }

    /// return the UnspentOutputDatabase internal to make remove faster
    /// pass in to UnspentOutputDatabase::remove() if available
    inline uint64_t rmHint() const {
        return m_uo.rmHint();
    }

private:
    UnspentOutput m_uo;
    int m_txVer = -1;
    std::int64_t m_outputValue = -1;
    Streaming::ConstBuffer m_outputScript;
};

#endif
