/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2015 The Bitcoin Core developers
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

#ifndef FLOWEE_PARTIALMERKLETREE_H
#define FLOWEE_PARTIALMERKLETREE_H

#include "streaming/P2PParser.h"
#include "serialize.h"
#include "uint256.h"
#include <vector>

/** Data structure that represents a partial merkle tree.
 *
 * It represents a subset of the txid's of a known block, in a way that
 * allows recovery of the list of txid's and the merkle root, in an
 * authenticated way.
 *
 * The encoding works as follows: we traverse the tree in depth-first order,
 * storing a bit for each traversed node, signifying whether the node is the
 * parent of at least one matched leaf txid (or a matched txid itself). In
 * case we are at the leaf level, or this bit is 0, its merkle node hash is
 * stored, and its children are not explorer further. Otherwise, no hash is
 * stored, but we recurse into both (or the only) child branch. During
 * decoding, the same depth-first traversal is performed, consuming bits and
 * hashes as they written during encoding.
 *
 * The serialization is fixed and provides a hard guarantee about the
 * encoded size:
 *
 *   SIZE <= 10 + ceil(32.25*N)
 *
 * Where N represents the number of leaf nodes of the partial tree. N itself
 * is bounded by:
 *
 *   N <= total_transactions
 *   N <= 1 + matched_transactions*tree_height
 *
 * The serialization format:
 *  - uint32     total_transactions (4 bytes)
 *  - varint     number of hashes   (1-3 bytes)
 *  - uint256[]  hashes in depth-first order (<= 32*N bytes)
 *  - varint     number of bytes of flag bits (1-3 bytes)
 *  - byte[]     flag bits, packed per 8 in a byte, least significant bit first (<= 2*N-1 bits)
 * The size constraints follow from this.
 */
class CPartialMerkleTree
{
protected:
    /** the total number of transactions in the block */
    unsigned int nTransactions;

    /** node-is-parent-of-matched-txid bits */
    std::vector<bool> vBits;

    /** txids and internal hashes */
    std::vector<uint256> vHash;

    /** flag set when encountering invalid data */
    bool fBad;

    /** helper function to efficiently calculate the number of nodes at given height in the merkle tree */
    unsigned int CalcTreeWidth(int height);

    /** calculate the hash of a node in the merkle tree (at leaf level: the txid's themselves) */
    uint256 CalcHash(int height, unsigned int pos, const std::vector<uint256> &vTxid);

    /** recursive function that traverses tree nodes, storing the data as bits and hashes */
    void TraverseAndBuild(int height, unsigned int pos, const std::vector<uint256> &vTxid, const std::vector<bool> &vMatch);

    /**
     * recursive function that traverses tree nodes, consuming the bits and hashes produced by TraverseAndBuild.
     * it returns the hash of the respective node.
     */
    uint256 TraverseAndExtract(int height, unsigned int pos, unsigned int &nBitsUsed, unsigned int &nHashUsed, std::vector<uint256> &vMatch);

public:

    /** serialization implementation */
    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nTransactions);
        READWRITE(vHash);
        std::vector<unsigned char> vBytes;
        if (ser_action.ForRead()) {
            READWRITE(vBytes);
            CPartialMerkleTree &us = *(const_cast<CPartialMerkleTree*>(this));
            us.vBits.resize(vBytes.size() * 8);
            for (unsigned int p = 0; p < us.vBits.size(); p++)
                us.vBits[p] = (vBytes[p / 8] & (1 << (p % 8))) != 0;
            us.fBad = false;
        } else {
            vBytes.resize((vBits.size()+7)/8);
            for (unsigned int p = 0; p < vBits.size(); p++)
                vBytes[p / 8] |= vBits[p] << (p % 8);
            READWRITE(vBytes);
        }
    }

    static CPartialMerkleTree construct(Streaming::P2PParser &parser);

    /** Construct a partial merkle tree from a list of transaction ids, and a mask that selects a subset of them */
    CPartialMerkleTree(const std::vector<uint256> &vTxid, const std::vector<bool> &vMatch);

    CPartialMerkleTree();

    /**
     * extract the matching txid's represented by this partial merkle tree.
     * returns the merkle root, or 0 in case of failure
     */
    uint256 ExtractMatches(std::vector<uint256> &vMatch);
};

#endif
