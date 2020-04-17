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
#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include "BlockHeader.h"

#include <streaming/P2PBuilder.h>
#include <Message.h>
#include <arith_uint256.h>
#include <uint256.h>

#include <boost/unordered_map.hpp>
#include <mutex>

class DownloadManager;

class Blockchain
{
public:
    Blockchain(DownloadManager *downloadManager);

    Message createGetHeadersRequest(Streaming::P2PBuilder &builder);

    void processBlockHeaders(Message message, int peerId);

    int expectedBlockHeight() const;

    bool isKnown(const uint256 &blockId) const;
    int blockHeightFor(const uint256 &blockId) const;

    BlockHeader block(int height) const;

private:
    mutable std::mutex m_lock;
    std::vector<BlockHeader> m_longestChain;
    struct ChainTip {
        uint256 tip;
        int height;
        arith_uint256 chainWork;
    };
    ChainTip m_tip;

    typedef boost::unordered_map<uint256, int, HashShortener> BlockHeightMap;
    BlockHeightMap m_blockHeight;

    DownloadManager *m_dlmanager;

    // consensus
    const uint256 powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    std::map<int, uint256> checkpoints;
};

#endif
