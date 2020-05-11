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
#include "BlockHeader.h"

#include <utils/streaming/P2PParser.h>
#include <utils/hash.h>
#include <streaming/P2PBuilder.h>

BlockHeader BlockHeader::fromMessage(Streaming::P2PParser &parser)
{
    BlockHeader answer;
    answer.nVersion = parser.readInt();
    answer.hashPrevBlock = parser.readUint256();
    answer.hashMerkleRoot = parser.readUint256();
    answer.nTime = parser.readInt();
    answer.nBits = parser.readInt();
    answer.nNonce = parser.readInt();

    return answer;
}

BlockHeader BlockHeader::fromMessage(const Streaming::ConstBuffer &buffer)
{
    Streaming::P2PParser parser(buffer);
    return fromMessage(parser);
}

uint256 BlockHeader::createHash() const
{
    static_assert (sizeof(*this) == 80, "Header size");
    assert(!hashMerkleRoot.IsNull());
    CHash256 hasher;
    hasher.Write(reinterpret_cast<const uint8_t*>(this), 80);
    uint256 hash;
    hasher.Finalize((unsigned char*)&hash);
    return hash;
}

arith_uint256 BlockHeader::blockProof() const
{
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a arith_uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}

Streaming::ConstBuffer BlockHeader::write(Streaming::BufferPool &pool) const
{
    pool.reserve(80);
    Streaming::P2PBuilder builder(pool);
    builder.writeInt(nVersion);
    builder.writeByteArray(hashPrevBlock, Streaming::RawBytes);
    builder.writeByteArray(hashMerkleRoot, Streaming::RawBytes);
    builder.writeInt(nTime);
    builder.writeInt(nBits);
    builder.writeInt(nNonce);
    return builder.buffer();
}
