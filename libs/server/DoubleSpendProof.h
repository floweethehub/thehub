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
#ifndef DOUBLESPENDPROOF_H
#define DOUBLESPENDPROOF_H

#include <uint256.h>
#include <primitives/FastTransaction.h>
#include "serialize.h"
#include <deque>

class CTxMemPool;

class DoubleSpendProof
{
public:
    DoubleSpendProof();

    static DoubleSpendProof create(const Tx &tx1, const Tx &tx2);

    bool isEmpty() const;

    enum Validity {
        Valid,
        MissingTransction,
        MissingUTXO,
        AlreadyMined,
        Invalid
    };

    Validity validate(const CTxMemPool &mempool) const;

    uint256 prevTxId() const;
    int prevOutIndex() const;

    struct Spender {
        uint32_t txVersion = 0, outSequence = 0, lockTime = 0;
        uint256 hashPrevOutputs, hashSequence, hashOutputs;
        std::vector<std::vector<uint8_t>> pushData;
    };

    Spender firstSpender() const {
        return m_spender1;
    }
    Spender doubleSpender() const {
        return m_spender2;
    }

    // old fashioned serialization.
    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(m_prevTxId);
        READWRITE(m_prevOutIndex);

        READWRITE(m_spender1.txVersion);
        READWRITE(m_spender1.outSequence);
        READWRITE(m_spender1.lockTime);
        READWRITE(m_spender1.hashPrevOutputs);
        READWRITE(m_spender1.hashSequence);
        READWRITE(m_spender1.hashOutputs);
        READWRITE(m_spender1.pushData);

        READWRITE(m_spender2.txVersion);
        READWRITE(m_spender2.outSequence);
        READWRITE(m_spender2.lockTime);
        READWRITE(m_spender2.hashPrevOutputs);
        READWRITE(m_spender2.hashSequence);
        READWRITE(m_spender2.hashOutputs);
        READWRITE(m_spender2.pushData);
    }

    uint256 createHash() const;

private:
    uint256 m_prevTxId;
    int32_t m_prevOutIndex = -1;

    Spender m_spender1, m_spender2;
};

#endif
