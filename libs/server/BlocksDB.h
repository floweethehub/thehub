/*
 * This file is part of the Flowee project
 * Copyright (c) 2009-2010 Satoshi Nakamoto
 * Copyright (c) 2009-2015 The Bitcoin Core developers
 * Copyright (c) 2017-2018 Tom Zander <tomz@freedommail.ch>
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

#ifndef BLOCKSDB_H
#define BLOCKSDB_H

#include "dbwrapper.h"

#include <primitives/FastUndoBlock.h>
#include <streaming/ConstBuffer.h>
#include <string>
#include <vector>

class CBlockFileInfo;
class CBlockIndex;
struct CDiskTxPos;
struct CDiskBlockPos;
class uint256;
class FastBlock;
class CChain;
class CScheduler;

namespace Blocks {

enum ReindexingState {
    NoReindex,
    ScanningFiles,
    ParsingBlocks
};

class DBPrivate;

/** Access to the block database (blocks/index/) */
class DB : public CDBWrapper
{
public:
    /**
     * returns the singleton instance of the BlocksDB. Please be aware that
     *     this will return nullptr until you call createInstance() or createTestInstance();
     */
    static DB *instance();
    /**
     * Deletes an old and creates a new instance of the BlocksDB singleton.
     * @param[in] nCacheSize  Configures various leveldb cache settings.
     * @param[in] fWipe       If true, remove all existing data.
     * @see instance()
     */
    static void createInstance(size_t nCacheSize, bool fWipe, CScheduler *scheduler = nullptr);
    /// Deletes old singleton and creates a new one for unit testing.
    static void createTestInstance(size_t nCacheSize);
    static void shutdown();

    /**
     * @brief starts the blockImporter part of a 'reindex'.
     * This kicks off a new thread that reads each file and schedules each block for
     * validation.
     */
    static void startBlockImporter();

protected:
    DB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    DB(const Blocks::DB&) = delete;
    void operator=(const DB&) = delete;
public:
    bool WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo *> > &fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo);
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &fileinfo);
    bool ReadLastBlockFile(int &nFile);
    bool ReadTxIndex(const uint256 &txid, CDiskTxPos &pos);
    bool WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> > &list);
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);
    /// Reads and caches all info about blocks.
    bool CacheAllBlockInfos();

    ReindexingState reindexing() const;
    inline bool isReindexing() const {
        return reindexing() != NoReindex;
    }
    void setReindexing(ReindexingState state);

    FastBlock loadBlock(CDiskBlockPos pos);
    FastUndoBlock loadUndoBlock(CDiskBlockPos pos);
    Streaming::ConstBuffer loadBlockFile(int fileIndex);
    FastBlock writeBlock(const FastBlock &block, CDiskBlockPos &pos);
    /**
     * @brief This method writes out the undo block to a specific file and belonging to a specific /a blockHash.
     * @param block The actual undo block
     * @param blockHash the hash of the parent block
     * @param fileIndex the index the original block was written to, this determines which revert index this block goes to.
     * @param posInFile a return value of the position this block ended up in.
     */
    void writeUndoBlock(const UndoBlockBuilder &undoBlock, int fileIndex, uint32_t *posInFile = 0);

    /**
     * @brief make the blocks-DB aware of a new header-only tip.
     * Add the parially validated block to the blocks database and import all parent
     * blocks at the same time.
     * This potentially updates the headerChain() and headerChainTips().
     * @param block the index to the block object.
     * @returns true if the header became the new main-chain tip.
     */
    bool appendHeader(CBlockIndex *block);
    /// allow adding one block, this API is primarily meant for unit tests.
    bool appendBlock(CBlockIndex *block, int lastBlockFile);

    const CChain &headerChain();
    const std::list<CBlockIndex*> & headerChainTips();

    void loadConfig();

    /// \internal
    std::shared_ptr<DBPrivate> priv() {
        return d;
    }

private:
    static DB *s_instance;
    std::shared_ptr<DBPrivate> d;
};

/** Open a block file (blk?????.dat) */
FILE* openFile(const CDiskBlockPos &pos, bool fReadOnly);
/** Open an undo file (rev?????.dat) */
FILE* openUndoFile(const CDiskBlockPos &pos, bool fReadOnly);
/**
 * Translation to a filesystem path.
 * @param fileIndex the number. For instance blk12345.dat is 12345.
 * @param prefix either "blk" or "rev"
 * @param fFindHarder set this to true if you want a path outside our main data-directory
 */
boost::filesystem::path getFilepathForIndex(int fileIndex, const char *prefix, bool fFindHarder = false);


namespace Index {
    const uint256 *insert(const uint256 &hash, CBlockIndex *index);
    bool exists(const uint256 &hash);
    CBlockIndex *get(const uint256 &hash);
    bool empty();
    int size();
    bool reconsiderBlock(CBlockIndex *pindex);

    /** Find the last common ancestor two blocks have.
     *  Both pa and pb must be non-NULL. */
    CBlockIndex* lastCommonAncestor(CBlockIndex* pa, CBlockIndex* pb);

    /**
     * @brief fileIndexes loops over all blocks to find indexes.
     * @return a set of file-indexes (blk[num].dat) that contain blocks.
     */
    std::set<int> fileIndexes();
    /**
     * @brief allByHeight Sort and return the blocks by height.
     */
    std::vector<std::pair<int, CBlockIndex*> > allByHeight();
    void unload();
}
}



#endif
