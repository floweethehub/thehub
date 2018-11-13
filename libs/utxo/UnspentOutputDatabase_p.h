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
#include "FloweeCOWList.h"
#include <streaming/BufferPool.h>

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <unordered_map>
#include <list>
#include <set>
#include <mutex>
#include <uint256.h>

namespace {
    inline std::uint32_t createShortHash(uint64_t cheapHash) {
        std::uint32_t answer = static_cast<uint32_t>(cheapHash & 0xFF) << 12;
        answer += (cheapHash & 0xFF00) >> 4;
        answer += (cheapHash & 0xF00000) >> 20;
        return answer;
    }
}

struct OutputRef {
    OutputRef() = default;
    OutputRef(uint64_t cheapHash, uint32_t leafPos)
        : cheapHash(cheapHash), leafPos(leafPos) {
    }
    inline bool operator==(const OutputRef &other) const {
        return cheapHash == other.cheapHash && leafPos == other.leafPos;
    }
    inline bool operator!=(const OutputRef &other) const { return !operator==(other); }
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
    int32_t saveToDisk(Streaming::BufferPool &pool) const;
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
        PositionInFile,

        // Additional bucket-positioning tags
        LeafPosOn512MB,
        LeafPosFromPrevLeaf

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
    boost::filesystem::path filenameFor(int index) const;

    std::string writeInfoFile(DataFile *source);
    bool load(const InfoFile &info, DataFile *target);

    std::list<InfoFile> m_validInfoFiles;
private:
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
   * ones in m_leafs.
   */
public:
    DataFile(const boost::filesystem::path &filename);

    void insert(const UODBPrivate *priv, const uint256 &txid, int outIndex, int blockHeight, int offsetInBlock);
    void insertAll(const UODBPrivate *priv, const UnspentOutputDatabase::BlockData &data);
    UnspentOutput find(const uint256 &txid, int index) const;
    SpentOutput remove(const UODBPrivate *priv, const uint256 &txid, int index, uint32_t leafHint = 0);

    // writing to disk. Return if there are still unsaved items left
    bool flushSomeNodesToDisk(ForceBool force);
    std::string flushAll();
    int32_t saveLeaf(const UnspentOutput &uo);

    // session management.
    void commit();
    void rollback();

    // update m_changeCount
    void addChange(const UODBPrivate *priv);

    bool openInfo(int targetHeight);

    bool m_jumptableNeedsSave = false;
    bool m_fileFull = false;

    // in-memory representation
    Streaming::BufferPool m_memBuffers;
    uint32_t m_jumptables[0x100000];
    std::unordered_map<int, Bucket> m_buckets;
    int m_nextBucketIndex = 1;
    // unsaved leafs.
    std::unordered_map<int, UnspentOutput> m_leafs;
    int m_nextLeafIndex = 1;

    // on-disk file.
    const boost::filesystem::path m_path;
    std::shared_ptr<char> m_buffer;
    Streaming::BufferPool m_writeBuffer;
    boost::iostreams::mapped_file m_file;

    /// wipes and creates a new datafile
    static DataFile *createDatafile(const boost::filesystem::path &filename, int firstBlockindex, const uint256 &firstHash);

    mutable std::recursive_mutex m_lock, m_saveLock;

    int m_initialBlockHeight = 0;
    int m_lastBlockHeight = 0;
    uint256 m_lastBlockHash;

    // Amount of inserts/deletes since last flush
    int m_changeCount = 0;
    int m_changesSinceJumptableWritten = 0;
    bool m_flushScheduled = false;

    // --- rollback info ---
    std::list<UnspentOutput> m_leafsBackup; //< contains leafs deleted and never saved
    /// contains leaf-ids deleted related to a certain bucketId (so they can be re-added to bucket)
    std::list<OutputRef> m_leafIdsBackup;
    /// buckets that were in memory when we committed last and have since been modified. We refuse to save them (for now).
    /// all values should have MEMBIT set
    std::set<uint32_t> m_bucketsToNotSave;
    /// buckets that have a good state on disk, have been loaded into memory to add
    /// or remove something and thus the jumptable forgot where on disk the original was.
    /// shorthash -> position on disk
    std::unordered_map<uint32_t, uint32_t> m_committedBucketLocations;
    uint32_t m_lastCommittedBucketIndex = 0;
    uint32_t m_lastCommittedLeafIndex = 0;

    mutable std::atomic_int m_usageCount;

    struct LockGuard {
        LockGuard(const DataFile *parent) : parent(parent) {
            assert(parent);
            ++parent->m_usageCount;
        }
        ~LockGuard() {
            if (!--parent->m_usageCount)
                delete parent;
        }
        void deleteLater() {
            --parent->m_usageCount;
        }
        const DataFile *parent = nullptr;
    };
};

struct Limits
{
    uint32_t DBFileSize = 2147483600; // 2GiB
    uint32_t FileFull = 1800000000; // 1.8GB
    uint32_t AutoFlush = 5000000; // every 5 million inserts/deletes, auto-flush jumptables
};

class UODBPrivate
{
public:
    UODBPrivate(boost::asio::io_service &service,  const boost::filesystem::path &basedir);

    // find existing DataFiles
    void init();

    boost::filesystem::path filepathForIndex(int fileIndex);

    boost::asio::io_service& ioService;

    bool memOnly = false; //< if true, we never flush to disk.
    bool doPrune = false;

    const boost::filesystem::path basedir;

    COWList<DataFile*> dataFiles;

    static Limits limits;
};

#endif
