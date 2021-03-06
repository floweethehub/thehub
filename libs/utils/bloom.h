/*
 * This file is part of the Flowee project
 * Copyright (C) 2012-2015 The Bitcoin Core developers
 * Copyright (C) 2020 Tom Zander <tom@flowee.org>
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

#ifndef FLOWEE_BLOOM_H
#define FLOWEE_BLOOM_H

#include "serialize.h"

#include <streaming/ConstBuffer.h>
#include <vector>

class COutPoint;
class CTransaction;
class uint256;
namespace Streaming {
  class P2PBuilder;
}

//! 20,000 items with fp rate < 0.1% or 10,000 items and <0.0001%
static const unsigned int MAX_BLOOM_FILTER_SIZE = 36000; // bytes
static const unsigned int MAX_HASH_FUNCS = 50;

/**
 * First two bits of nFlags control how much IsRelevantAndUpdate actually updates
 * The remaining bits are reserved
 */
enum BloomFlags
{
    BLOOM_UPDATE_NONE = 0,
    BLOOM_UPDATE_ALL = 1,
    // Only adds outpoints to the filter if the output is a pay-to-pubkey/pay-to-multisig script
    BLOOM_UPDATE_P2PUBKEY_ONLY = 2,
    BLOOM_UPDATE_MASK = 3,
};

/**
 * BloomFilter is a probabilistic filter which SPV clients provide
 * so that we can filter the transactions we send them.
 * 
 * This allows for significantly more efficient transaction and block downloads.
 * 
 * Because bloom filters are probabilistic, a SPV node can increase the false-
 * positive rate, making us send it transactions which aren't actually its,
 * allowing clients to trade more bandwidth for more privacy by obfuscating which
 * keys are controlled by them.
 */
class CBloomFilter
{
private:
    std::vector<unsigned char> m_data;
    bool m_isFull;
    bool m_isEmpty;
    uint32_t m_numHashFuncs;
    uint32_t m_tweak;
    uint8_t m_flags;

    uint32_t hash(uint32_t nHashNum, const std::vector<uint8_t> &vDataToHash) const;

    // Private constructor for CRollingBloomFilter, no restrictions on size
    CBloomFilter(unsigned int nElements, double nFPRate, unsigned int tweak);
    friend class CRollingBloomFilter;

public:
    /**
     * Creates a new bloom filter which will provide the given fp rate when filled with the given number of elements
     * Note that if the given parameters will result in a filter outside the bounds of the protocol limits,
     * the filter created will be as close to the given parameters as possible within the protocol limits.
     * This will apply if nFPRate is very low or nElements is unreasonably high.
     * nTweak is a constant which is added to the seed value passed to the hash function
     * It should generally always be a random value (and is largely only exposed for unit testing)
     * nFlags should be one of the BLOOM_UPDATE_* enums (not _MASK)
     */
    CBloomFilter(unsigned int nElements, double nFPRate, unsigned int tweak, unsigned char flags);
    CBloomFilter() : m_isFull(true), m_isEmpty(false), m_numHashFuncs(0), m_tweak(0), m_flags(0) {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(m_data);
        READWRITE(m_numHashFuncs);
        READWRITE(m_tweak);
        READWRITE(m_flags);
    }

    uint8_t flags() const { return m_flags; }

    void store(Streaming::P2PBuilder &builder) const;

    void insert(const std::vector<unsigned char> &vKey);
    void insert(const COutPoint &outpoint);
    void insert(const uint256 &hash);
    void insert(const Streaming::ConstBuffer &buf);

    bool contains(const std::vector<unsigned char>& vKey) const;
    bool contains(const COutPoint& outpoint) const;
    bool contains(const uint256& hash) const;

    void clear();
    void reset(unsigned int nNewTweak);

    //! True if the size is <= MAX_BLOOM_FILTER_SIZE and the number of hash functions is <= MAX_HASH_FUNCS
    //! (catch a filter which was just deserialized which was too big)
    bool isWithinSizeConstraints() const;

    //! Scans output scripts for matches and adds those outpoints to the filter
    //! for spend detection. Returns true if any output matched, or the txid
    //! matches.
    bool matchAndInsertOutputs(const CTransaction &tx);

    //! Scan inputs to see if the spent outpoints are a match, or the input
    //! scripts contain matching elements.
    bool matchInputs(const CTransaction &tx);

    bool isRelevantAndUpdate(const CTransaction &tx) {
        return matchAndInsertOutputs(tx) || matchInputs(tx);
    }

    //! Checks for empty and full filters to avoid wasting cpu
    void updateEmptyFull();
    /// returns true if this is a valid but empty filter
    bool isEmpty() const;
};

/**
 * RollingBloomFilter is a probabilistic "keep track of most recently inserted" set.
 * Construct it with the number of items to keep track of, and a false-positive
 * rate. Unlike CBloomFilter, by default nTweak is set to a cryptographically
 * secure random value for you. Similarly rather than clear() the method
 * reset() is provided, which also changes nTweak to decrease the impact of
 * false-positives.
 *
 * contains(item) will always return true if item was one of the last N things
 * insert()'ed ... but may also return true for items that were not inserted.
 */
class CRollingBloomFilter
{
public:
    // A random bloom filter calls GetRand() at creation time.
    // Don't create global CRollingBloomFilter objects, as they may be
    // constructed before the randomizer is properly initialized.
    CRollingBloomFilter(unsigned int nElements, double nFPRate);

    void insert(const std::vector<unsigned char>& vKey);
    void insert(const uint256& hash);
    bool contains(const std::vector<unsigned char>& vKey) const;
    bool contains(const uint256& hash) const;

    void reset();
    inline void clear() {
        if (nInsertions > 0)
            reset();
    }

private:
    unsigned int nBloomSize;
    unsigned int nInsertions;
    CBloomFilter b1, b2;
};


#endif
