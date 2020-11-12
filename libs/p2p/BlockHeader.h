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
#ifndef BLOCKHEADER_H
#define BLOCKHEADER_H

#include <uint256.h>
#include <cstdint>
#include <arith_uint256.h>

namespace Streaming {
    class P2PParser;
    class BufferPool;
    class ConstBuffer;
}

struct BlockHeader
{
    static BlockHeader fromMessage(Streaming::P2PParser &parser);
    static BlockHeader fromMessage(const Streaming::ConstBuffer &buffer);

    uint256 createHash() const;
    arith_uint256 blockProof() const;
    // write the header in P2P syntax (just like on the blockchain)
    Streaming::ConstBuffer write(Streaming::BufferPool &pool) const;

    int32_t nVersion = 0;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime = 0;
    uint32_t nBits = 0;
    uint32_t nNonce = 0;
};

#endif
