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
#include <primitives/script.h>
#include "serialize.h"
#include <deque>

class CTxMemPool;

class DoubleSpendProof
{
public:
    //! limit for the size of a `pushData` vector below
    static constexpr size_t MaxPushDataSize = MAX_SCRIPT_ELEMENT_SIZE;

    /** Creates an empty, invalid object */
    DoubleSpendProof();

    /** Create a proof object, given two conflicting transactions */
    static DoubleSpendProof create(const Tx &tx1, const Tx &tx2);

    /** Returns true if this object is invalid, i.e. does not represent a double spend proof */
    bool isEmpty() const;

    /** Return codes for the 'validate' function */
    enum Validity {
        Valid, //< Double spend proof is valid
        MissingTransaction, //< We cannot determine the validity of this proof because we don't have one of the spends
        MissingUTXO, //< We cannot determine the validity of this proof because the prevout is not available
        Invalid //< This object does not contain a valid doublespend proof
    };

    /**
     * Returns whether this doublespend proof is valid, or why its
     * validity cannot be determined.
     */
    Validity validate(const CTxMemPool &mempool) const;

    /** Returns the hash of the input transaction (UTXO) that is being doublespent */
    uint256 prevTxId() const;
    /** Returns the index of the output that is being doublespent */
    int prevOutIndex() const;

    /** This struction tracks information about each doublespend transaction */
    struct Spender {
        uint32_t txVersion = 0, outSequence = 0, lockTime = 0;
        uint256 hashPrevOutputs, hashSequence, hashOutputs;
        std::vector<std::vector<uint8_t>> pushData;
    };

    /// Return the first spender, sorted by hashOutputs
    Spender firstSpender() const {
        return m_spender1;
    }
    /// return the 2nd spender, sorted by hashOutputs
    Spender secondSpender() const {
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

    /** create the ID of this doublespend proof */
    uint256 createHash() const;

private:
    /// Throws std::runtime_error if the proof breaks the sanity of:
    /// - isEmpty()
    /// - does not have exactly 1 pushData per spender vector
    /// - any pushData size >520 bytes
    /// Called from: `create()` and `validate()` (`validate()` won't throw but will return Invalid)
    void checkSanityOrThrow() const;


    uint256 m_prevTxId;
    int32_t m_prevOutIndex = -1;

    Spender m_spender1, m_spender2;
};

#endif
