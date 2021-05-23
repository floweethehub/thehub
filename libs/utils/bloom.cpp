/*
 * This file is part of the Flowee project
 * Copyright (c) 2012-2015 The Bitcoin Core developers
 * Copyright (c) 2020 Tom Zander <tomz@freedommail.ch>
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

#include "bloom.h"

#include "primitives/transaction.h"
#include "hash.h"
#include "primitives/script.h"
#include "random.h"
#include "streaming/streams.h"

#include <cmath>

#include <streaming/P2PBuilder.h>

#define LN2SQUARED 0.4804530139182014246671025263266649717305529515945455
#define LN2 0.6931471805599453094172321214581765680755001343602552

CBloomFilter::CBloomFilter(unsigned int nElements, double nFPRate, unsigned int nTweakIn, unsigned char nFlagsIn) :
    /**
     * The ideal size for a bloom filter with a given number of elements and false positive rate is:
     * - nElements * log(fp rate) / ln(2)^2
     * We ignore filter parameters which will create a bloom filter larger than the protocol limits
     */
    m_data(std::min((unsigned int)(-1  / LN2SQUARED * nElements * log(nFPRate)), MAX_BLOOM_FILTER_SIZE * 8) / 8),
    /**
     * The ideal number of hash functions is filter size * ln(2) / number of elements
     * Again, we ignore filter parameters which will create a bloom filter with more hash functions than the protocol limits
     * See https://en.wikipedia.org/wiki/Bloom_filter for an explanation of these formulas
     */
    m_isFull(false),
    m_isEmpty(false),
    m_numHashFuncs(std::min((unsigned int)(m_data.size() * 8 / nElements * LN2), MAX_HASH_FUNCS)),
    m_tweak(nTweakIn),
    m_flags(nFlagsIn)
{
}

// Private constructor used by CRollingBloomFilter
CBloomFilter::CBloomFilter(unsigned int nElements, double nFPRate, unsigned int nTweakIn) :
    m_data((unsigned int)(-1  / LN2SQUARED * nElements * log(nFPRate)) / 8),
    m_isFull(false),
    m_isEmpty(true),
    m_numHashFuncs((unsigned int)(m_data.size() * 8 / nElements * LN2)),
    m_tweak(nTweakIn),
    m_flags(BLOOM_UPDATE_NONE)
{
}

bool CBloomFilter::isEmpty() const
{
    return m_isEmpty;
}

uint32_t CBloomFilter::hash(uint32_t nHashNum, const std::vector<uint8_t> &vDataToHash) const
{
    // 0xFBA4C795 chosen as it guarantees a reasonable bit difference between nHashNum values.
    return MurmurHash3(nHashNum * 0xFBA4C795 + m_tweak, vDataToHash) % (m_data.size() * 8);
}

void CBloomFilter::insert(const std::vector<unsigned char> &vKey)
{
    if (m_isFull)
        return;
    for (unsigned int i = 0; i < m_numHashFuncs; i++)
    {
        unsigned int nIndex = hash(i, vKey);
        // Sets bit nIndex of vData
        m_data[nIndex >> 3] |= (1 << (7 & nIndex));
    }
    m_isEmpty = false;
}

void CBloomFilter::insert(const COutPoint &outpoint)
{
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << outpoint;
    std::vector<unsigned char> data(stream.begin(), stream.end());
    insert(data);
}

void CBloomFilter::insert(const uint256 &hash)
{
    std::vector<unsigned char> data(hash.begin(), hash.end());
    insert(data);
}

void CBloomFilter::insert(const Streaming::ConstBuffer &buf)
{
    std::vector<unsigned char> data(buf.begin(), buf.end());
    insert(data);
}

bool CBloomFilter::contains(const std::vector<unsigned char> &vKey) const
{
    if (m_isFull)
        return true;
    if (m_isEmpty)
        return false;
    for (unsigned int i = 0; i < m_numHashFuncs; i++)
    {
        unsigned int nIndex = hash(i, vKey);
        // Checks bit nIndex of vData
        if (!(m_data[nIndex >> 3] & (1 << (7 & nIndex))))
            return false;
    }
    return true;
}

bool CBloomFilter::contains(const COutPoint &outpoint) const
{
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << outpoint;
    std::vector<unsigned char> data(stream.begin(), stream.end());
    return contains(data);
}

bool CBloomFilter::contains(const uint256 &hash) const
{
    std::vector<unsigned char> data(hash.begin(), hash.end());
    return contains(data);
}

void CBloomFilter::clear()
{
    m_data.assign(m_data.size(), 0);
    m_isFull = false;
    m_isEmpty = true;
}

void CBloomFilter::reset(unsigned int nNewTweak)
{
    clear();
    m_tweak = nNewTweak;
}

bool CBloomFilter::isWithinSizeConstraints() const
{
    return m_data.size() <= MAX_BLOOM_FILTER_SIZE && m_numHashFuncs <= MAX_HASH_FUNCS;
}

bool CBloomFilter::matchAndInsertOutputs(const CTransaction& tx)
{
    bool fFound = false;
    // Match if the filter contains the hash of tx
    //  for finding tx when they appear in a block
    if (m_isFull)
        return true;
    if (m_isEmpty)
        return false;
    const uint256& hash = tx.GetHash();
    if (contains(hash))
        fFound = true;

    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];
        // Match if the filter contains any arbitrary script data element in any scriptPubKey in tx
        // If this matches, also add the specific output that was matched.
        // This means clients don't have to update the filter themselves when a new relevant tx 
        // is discovered in order to find spending transactions, which avoids round-tripping and race conditions.
        CScript::const_iterator pc = txout.scriptPubKey.begin();
        std::vector<unsigned char> data;
        while (pc < txout.scriptPubKey.end())
        {
            opcodetype opcode;
            if (!txout.scriptPubKey.GetOp(pc, opcode, data))
                break;
            if (data.size() != 0 && contains(data))
            {
                fFound = true;
                if ((m_flags & BLOOM_UPDATE_MASK) == BLOOM_UPDATE_ALL)
                    insert(COutPoint(hash, i));
                else if ((m_flags & BLOOM_UPDATE_MASK) == BLOOM_UPDATE_P2PUBKEY_ONLY)
                {
                    Script::TxnOutType type;
                    std::vector<std::vector<unsigned char> > vSolutions;
                    if (Script::solver(txout.scriptPubKey, type, vSolutions) &&
                            (type == Script::TX_PUBKEY || type == Script::TX_MULTISIG))
                        insert(COutPoint(hash, i));
                }
                break;
            }
        }
    }

    return fFound;
}

bool CBloomFilter::matchInputs(const CTransaction &tx) {
    if (m_isEmpty)
        return false;

    for (const CTxIn& txin : tx.vin) {
        // Match if the filter contains an outpoint tx spends
        if (contains(txin.prevout))
            return true;

        // Match if the filter contains any arbitrary script data element in any scriptSig in tx
        CScript::const_iterator pc = txin.scriptSig.begin();
        std::vector<unsigned char> data;
        while (pc < txin.scriptSig.end())
        {
            opcodetype opcode;
            if (!txin.scriptSig.GetOp(pc, opcode, data))
                break;
            if (data.size() != 0 && contains(data))
                return true;
        }
    }

    return false;
}

void CBloomFilter::updateEmptyFull()
{
    bool full = true;
    bool empty = true;
    for (unsigned int i = 0; i < m_data.size(); i++)
    {
        full &= m_data[i] == 0xff;
        empty &= m_data[i] == 0;
    }
    m_isFull = full;
    m_isEmpty = empty;
}

void CBloomFilter::store(Streaming::P2PBuilder &builder) const
{
    builder.writeByteArray(m_data, Streaming::WithLength);
    builder.writeInt(m_numHashFuncs);
    builder.writeInt(m_tweak);
    builder.writeByte(m_flags);
}

// ///////////////////////////////////////////////////////////////


CRollingBloomFilter::CRollingBloomFilter(unsigned int nElements, double fpRate) :
    b1(nElements * 2, fpRate, 0), b2(nElements * 2, fpRate, 0)
{
    // Implemented using two bloom filters of 2 * nElements each.
    // We fill them up, and clear them, staggered, every nElements
    // inserted, so at least one always contains the last nElements
    // inserted.
    nInsertions = 0;
    nBloomSize = nElements * 2;

    reset();
}

void CRollingBloomFilter::insert(const std::vector<unsigned char>& vKey)
{
    if (nInsertions == 0) {
        b1.clear();
    } else if (nInsertions == nBloomSize / 2) {
        b2.clear();
    }
    b1.insert(vKey);
    b2.insert(vKey);
    if (++nInsertions == nBloomSize) {
        nInsertions = 0;
    }
}

void CRollingBloomFilter::insert(const uint256& hash)
{
    std::vector<unsigned char> data(hash.begin(), hash.end());
    insert(data);
}

bool CRollingBloomFilter::contains(const std::vector<unsigned char>& vKey) const
{
    if (nInsertions < nBloomSize / 2) {
        return b2.contains(vKey);
    }
    return b1.contains(vKey);
}

bool CRollingBloomFilter::contains(const uint256& hash) const
{
    std::vector<unsigned char> data(hash.begin(), hash.end());
    return contains(data);
}

void CRollingBloomFilter::reset()
{
    unsigned int nNewTweak = GetRand(std::numeric_limits<unsigned int>::max());
    b1.reset(nNewTweak);
    b2.reset(nNewTweak);
    nInsertions = 0;
}
