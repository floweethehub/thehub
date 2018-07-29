/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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
#ifndef UNSPENTOUTPUTDATABASEPRIVATE_H
#define UNSPENTOUTPUTDATABASEPRIVATE_H

#include "UnspentOutputDatabase.h"
#include <streaming/BufferPool.h>

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <unordered_map>
#include <list>
#include <set>
#include <mutex>
#include <uint256.h>

struct OutputRef {
    OutputRef() = default;
    OutputRef(uint64_t cheapHash, uint32_t leafPos)
        : cheapHash(cheapHash), leafPos(leafPos) {
    }
    uint64_t cheapHash;
    uint32_t leafPos;
};

enum ForceBool {
    ForceSave,
    NormalSave
};
struct Bucket {
    std::list<OutputRef> unspentOutputs;
    short saveAttempt = 0;

    void fillFromDisk(const Streaming::ConstBuffer &buffer, const int32_t bucketOffsetInFile);
    int32_t saveToDisk(Streaming::BufferPool &pool);
};

namespace UODB {
    enum MessageTags {
        Separator = 0,

        // tags to store the leaf
        TXID,
        OutIndex,
        BlockHeight,
        OffsetInBlock,

        // tags to store the bucket
        LeafPosition,
        LeafPosRelToBucket,
        CheapHash,

        // tags to store the jump-index
        LastBlockId, // uint256
        FirstBlockHeight,
        LastBlockHeight,
        JumpTableHash,
        PositionInFile
    };
}

class DataFile;
class DataFileCache {
public:
    DataFileCache(const boost::filesystem::path &baseFilename);

    struct InfoFile {
        int index = -1;
        int initialBlockHeight = -1;
        int lastBlockHeight = -1;
    };

    InfoFile parseInfoFile(int index) const;

    void writeInfoFile(DataFile *source);
    bool load(const InfoFile &info, DataFile *target);

    std::list<InfoFile> m_validInfoFiles;
private:
    boost::filesystem::path filenameFor(int index) const;
    const boost::filesystem::path m_baseFilename;
};


// represents one storage-DB file.
class DataFile {
  /*
   * We start with the below array of 1 million unsigned ints.
   *
   * This starts zero-filled, as new entries come in we insert the offset at the right place.
   * We use the first 2.5 bytes of the prev-txid hash as index in the array.
   *
   * The offset points to a variable-length list.
   * We either use the on-disk datafile to store that list, or we store it in memory (because
   *  it has changed and we didn't flush yet).
   * The first bit in the offset decides between those two options. 1 = in-memory.
   * For the on-disk case, we just use the offset in the file.
   * For in-memory we use the lower 31 bits as offset in the bucket-list.
   *
   * Buckets have lists of OutputRefs. The first 64 bits of the prev-txid are used here as a shorthash,
   * we follow up with a leaf-pos which is again a pointer in the file, to UnspentOutput this time, or unsaved
   * ones in leafs.
   */
public:
    DataFile(const boost::filesystem::path &filename);

    void insert(const UODBPrivate *priv, const uint256 &txid, int outIndex, int blockHeight, int offsetInBlock);
    UnspentOutput find(const uint256 &txid, int index) const;
    SpentOutput remove(const UODBPrivate *priv, const uint256 &txid, int index);

    // writing to disk. Return if there are still unsaved items left
    bool flushSomeNodesToDisk(ForceBool force);
    void flushAll();
    int32_t saveLeaf(const UnspentOutput &uo);

    // session management.
    void commit();
    void rollback();

    bool m_jumptableNeedsSave = false;
    bool m_fileFull = false;

    // in-memory representation
    Streaming::BufferPool m_memBuffers;
    uint32_t m_jumptables[0x100000];
    std::unordered_map<int, Bucket> m_buckets;
    int m_bucketIndex = 1;
    // unsaved leafs.
    std::unordered_map<int, UnspentOutput> m_leafs;
    int m_leafIndex = 1;

    // on-disk file.
    const boost::filesystem::path m_path;
    std::shared_ptr<char> m_buffer;
    Streaming::BufferPool m_writeBuffer;
    boost::iostreams::mapped_file m_file;

    /// wipes and creates a new datafile
    static DataFile *createDatafile(const boost::filesystem::path &filename, int firstBlockindex, const uint256 &firstHash);

    mutable std::recursive_mutex m_lock;

    int m_initialBlockHeight = 0;
    int m_lastBlockheight = 0;
    uint256 m_lastBlockHash;

    // Amount of inserts/deletes since last flush
    int m_changeCount = 0;
    int m_changesSinceJumptableWritten = 0;
    bool m_flushScheduled = false;

    // rollback info
    int m_leafIndex_saved = 0;
    int m_bucketIndex_saved = 0;
    std::unordered_map<int, UnspentOutput> m_deletedLeafs;
    std::unordered_map<uint32_t, uint32_t> m_deletedBuckets; // shorthash to position-in-file
    std::unordered_map<int, Bucket> m_changedBuckets; // bucketId to bucket-copy
    std::unordered_map<int, uint32_t> m_committedJumptable; // when we load (and change) a bucket we need to remember where on disk the previous one was
};

class UODBPrivate
{
public:
    UODBPrivate(boost::asio::io_service &service,  const boost::filesystem::path &basedir);
    ~UODBPrivate();

    // find existing DataFiles
    void init();

    void flushNodesToDisk();
    boost::filesystem::path filepathForIndex(int fileIndex);

    boost::asio::io_service& ioService;

    bool memOnly = false; //< if true, we never flush to disk.

    const boost::filesystem::path basedir;

    // on-disk-data
    std::vector<DataFile*> dataFiles;

    mutable std::mutex lock;
};

#endif
