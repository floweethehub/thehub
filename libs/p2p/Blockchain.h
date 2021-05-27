/*
 * This file is part of the Flowee project
 * Copyright (C) 2020-2021 Tom Zander <tom@flowee.org>
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
#include "P2PNet.h"

#include <Message.h>
#include <arith_uint256.h>
#include <uint256.h>

#include <boost/filesystem.hpp>
#include <mutex>
#include <unordered_map>

class DownloadManager;
namespace Streaming {
class P2PBuilder;
}

class Blockchain
{
public:
    Blockchain(DownloadManager *downloadManager, const boost::filesystem::path &basedir, P2PNet::Chain chain);

    Message createGetHeadersRequest(Streaming::P2PBuilder &builder);

    void processBlockHeaders(Message message, int peerId);

    /**
     *
     */
    static void setStaticChain(const unsigned char *data, int64_t size);

    /**
     * Return the chain-height that we actually are at, based on validated headers.
     */
    int height() const;
    /**
     * Return the chain-height that based on the date/time we expect to be at.
     */
    int expectedBlockHeight() const;

    bool isKnown(const uint256 &blockId) const;
    int blockHeightFor(const uint256 &blockId) const;

    /**
     * Return the block header for a block at a certain height.
     * Height 0 is the genesis block.
     */
    BlockHeader block(int height) const;

    /// re-load the chain. Also called from the constructor.
    void load();
    /// save the chain
    void save();

private:
    void createMainchainGenesis();
    void loadMainchainCheckpoints();
    void createTestnet4Genesis();
    void loadTestnet4Checkpoints();
    void loadStaticChain(const unsigned char *data, int64_t dataSize);

    void createGenericGenesis(BlockHeader genesis);

    mutable std::mutex m_lock;
    const boost::filesystem::path m_basedir;
    std::vector<BlockHeader> m_longestChain;
    struct ChainTip {
        uint256 tip;
        int height;
        arith_uint256 chainWork;
    };
    ChainTip m_tip;

    typedef std::unordered_map<uint256, int, HashShortener, HashComparison> BlockHeightMap;
    BlockHeightMap m_blockHeight;

    DownloadManager *m_dlmanager;

    // consensus
    const uint256 powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    std::map<int, uint256> checkpoints;

    const unsigned char *m_staticChain = nullptr;
    int m_numStaticHeaders = 0; // including genesis, so this is height + 1
};

#endif
