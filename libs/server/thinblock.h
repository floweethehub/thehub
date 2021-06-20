/*
 * This file is part of the Flowee project
 * Copyright (C) 2015 G. Andrew Stone
 * Copyright (C) 2016 The Bitcoin Unlimited developers
 * Copyright (C) 2016 Tom Zander <tom@flowee.org>
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

#ifndef FLOWEE_THINBLOCK_H
#define FLOWEE_THINBLOCK_H

#include "serialize.h"
#include "uint256.h"
#include "primitives/block.h"
#include "bloom.h"

#include "net.h"
#include "util.h"

#include <vector>

class CBlock;
class CNode;


class CXThinBlock
{
public:
    CBlockHeader header;
    std::vector<uint64_t> vTxHashes; // List of all transactions id's in the block
    std::vector<CTransaction> vMissingTx; // vector of transactions that did not match the bloom filter
    bool collision;

public:
    CXThinBlock(const CBlock& block, CBloomFilter* filter = 0); // Use the filter to determine which txns the client has
    CXThinBlock();

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(header);
        READWRITE(vTxHashes);
        READWRITE(vMissingTx);
    }

    inline CInv GetInv() { return CInv(MSG_BLOCK, header.GetHash()); }
    bool process(CNode* pfrom);
};

// This class is used for retrieving a list of still missing transactions after receiving a "thinblock" message.
// The CXThinBlockTx when recieved can be used to fill in the missing transactions after which it is sent
// back to the requestor.  This class uses a 64bit hash as opposed to the normal 256bit hash.
class CXThinBlockTx
{
public:
    /** Public only for unit testing */
    uint256 blockhash;
    std::vector<CTransaction> vMissingTx; // map of missing transactions

public:
    CXThinBlockTx(uint256 blockHash, std::vector<CTransaction>& vTx);
    CXThinBlockTx() {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(blockhash);
        READWRITE(vMissingTx);
    }
};

// This class is used for retrieving a list of still missing transactions after receiving a "thinblock" message.
// The CXThinBlockTx when recieved can be used to fill in the missing transactions after which it is sent
// back to the requestor.  This class uses a 64bit hash as opposed to the normal 256bit hash.
class CXRequestThinBlockTx
{
public:
    /** Public only for unit testing */
    uint256 blockhash;
    std::set<uint64_t> setCheapHashesToRequest; // map of missing transactions

public:
    CXRequestThinBlockTx(uint256 blockHash, std::set<uint64_t>& setHashesToRequest);
    CXRequestThinBlockTx() {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(blockhash);
        READWRITE(setCheapHashesToRequest);
    }
};

bool HaveThinblockNodes();
bool CheckThinblockTimer(const uint256 &hash);
inline bool IsThinBlocksEnabled() {
    return GetBoolArg("-use-thinblocks", false);
}
bool IsChainNearlySyncd();
CBloomFilter createSeededBloomFilter(const std::vector<uint256>& vOrphanHashes);
void LoadFilter(CNode *pfrom, CBloomFilter *filter);
void HandleBlockMessage(CNode *pfrom, const std::string &strCommand, const CBlock &block, const CInv &inv);


// TODO namespace the new methods?

// Checks to see if the node is configured in flowee.conf to be an expedited block source and if so, request them.
void CheckAndRequestExpeditedBlocks(CNode* pfrom);
void SendExpeditedBlock(CXThinBlock& thinBlock, unsigned char hops, const CNode* skip = nullptr);
void SendExpeditedBlock(const CBlock& block, const CNode* skip = nullptr);
void HandleExpeditedRequest(CDataStream& vRecv, CNode* pfrom);
bool IsRecentlyExpeditedAndStore(const uint256& hash);
// process incoming unsolicited block
void HandleExpeditedBlock(CDataStream& vRecv,CNode* pfrom);

extern CCriticalSection cs_xval;

enum {
  EXPEDITED_STOP   = 1,
  EXPEDITED_BLOCKS = 2,
  EXPEDITED_TXNS   = 4,
};

enum {
  EXPEDITED_MSG_HEADER   = 1,
  EXPEDITED_MSG_XTHIN    = 2,
};

#endif
