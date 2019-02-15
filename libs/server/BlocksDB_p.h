/*
 * This file is part of the Flowee project
 * Copyright (C) 2017-2018 Tom Zander <tomz@freedommail.ch>
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

#ifndef BLOCKSDB_P_H
#define BLOCKSDB_P_H

#include "chain.h"
#include "BlocksDB.h"
#include "streaming/ConstBuffer.h"

#include <vector>
#include <mutex>
#include <memory>
#include <list>

#include <boost/iostreams/device/mapped_file.hpp>

class CBlockIndex;
class CScheduler;

namespace Blocks {

struct DataFile {
    DataFile() : filesize(0) {}
    boost::iostreams::mapped_file file;
    std::weak_ptr<char> buffer;
    size_t filesize;
};

enum BlockType {
    ForwardBlock,
    RevertBlock
};

struct FileHistoryEntry {
    FileHistoryEntry(const std::shared_ptr<char> &dataFile, int64_t lastAccessed)
        : dataFile(dataFile), lastAccessed(lastAccessed) {}

    std::shared_ptr<char> dataFile;
    int64_t lastAccessed = 0;
};

class DBPrivate  : public std::enable_shared_from_this<DBPrivate> {
public:
    DBPrivate();
    ~DBPrivate();

    Streaming::ConstBuffer loadBlock(CDiskBlockPos pos, BlockType type);
    Streaming::ConstBuffer writeBlock(const std::deque<Streaming::ConstBuffer> &block, CDiskBlockPos &pos, BlockType type);
    void unloadIndexMap();
    void foundBlockFile(int index, const CBlockFileInfo &info);

    std::shared_ptr<char> mapFile(int fileIndex, BlockType type, size_t *size_out = 0, bool *isWritable = nullptr);

    // close files from filehistory that have been unused for some time.
    void setScheduler(CScheduler *scheduler);
    void closeFiles();
    void pruneFiles();

    CChain headersChain;
    std::list<CBlockIndex*> headerChainTips;

    std::vector<std::string> blocksDataDirs;

    std::recursive_mutex lock;
    std::vector<DataFile*> datafiles;
    std::vector<DataFile*> revertDatafiles;
    std::list<FileHistoryEntry> fileHistory; // keep the last 10 to avoid opening and closing files all the time.

    std::mutex blockIndexLock;

    typedef boost::unordered_map<uint256, CBlockIndex*, BlockHashShortener> BlockMap;
    BlockMap indexMap;

    ReindexingState reindexing = NoReindex;
};
}

#endif
