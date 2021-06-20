/*
 * This file is part of the Flowee project
 * Copyright (C) 2018-2020 Tom Zander <tom@flowee.org>
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

/*
 * WARNING USAGE OF THIS HEADER IS RESTRICTED.
 * This Header file is part of the private API and is meant to be used solely by the UTXO component.
 *
 * Usage of this API will likely mean your code will break in interesting ways in the future,
 * or even stop to compile.
 *
 * YOU HAVE BEEN WARNED!!
 */

#include "UnspentOutputDatabase.h"
#include "BucketMap.h"
#include "DataFileList.h"
#include <streaming/BufferPool.h>

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <unordered_map>
#include <list>
#include <set>
#include <mutex>
#include <uint256.h>

#define MEMBIT 0x80000000
#define MEMMASK 0x7FFFFFFF

namespace {
    inline std::uint32_t createShortHash(uint64_t cheapHash) {
        std::uint32_t answer = static_cast<uint32_t>(cheapHash & 0xFF) << 12;
        answer += (cheapHash & 0xFF00) >> 4;
        answer += (cheapHash & 0xF00000) >> 20;
        return answer;
    }
}

struct FlexLockGuard {
    FlexLockGuard(std::recursive_mutex &mutex) : mutex(mutex) { mutex.lock(); }
    inline ~FlexLockGuard() { unlock(); }
    inline void unlock() { if (m_locked) { mutex.unlock(); m_locked = false; } }

private:
    std::recursive_mutex &mutex;
    bool m_locked = true;
};

enum ForceBool {
    ForceSave,
    NormalSave
};

// used internally in the flush to disk method
struct SavedBucket {
    SavedBucket(const std::vector<OutputRef> &uo, uint32_t offset, int saveCount) : unspentOutputs(uo), offsetInFile(offset), saveCount(saveCount) {}
    std::vector<OutputRef> unspentOutputs;
    uint32_t offsetInFile;
    int saveCount = 0;
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
        LeafPosFromPrevLeaf,
        LeafPosRepeat,

        // Additional tags for the jump-index
        ChangesSincePrune,

        // Is only present and true in the info that is the latest, tip, DB.
        IsTip,

        // Initial size of the buckets section of the DB (just after pruning)
        InitialBucketSegmentSize,

        // In the worldvie wof this UTXO a block stored in the 'block-index'
        // that was invalid stores its sha256 blockId here.
        InvalidBlockHash
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
    DataFile(const boost::filesystem::path &filename, int beforeHeight = INT_MAX);
    // This constructor is only used for unit testing.
    DataFile(int startHeight, int endHeight);
    DataFile(const DataFile &) = delete;

    void insert(const UODBPrivate *priv, const uint256 &txid, int firstOutput, int lastOutput, int blockHeight, int offsetInBlock);
    void insertAll(const UODBPrivate *priv, const UnspentOutputDatabase::BlockData &data, size_t start, size_t end);
    UnspentOutput find(const uint256 &txid, int index) const;
    SpentOutput remove(const UODBPrivate *priv, const uint256 &txid, int index, uint32_t leafHint = 0);

    /// checks jumptable fragmentation, returns amount of bytes its larger than after latest prune
    int fragmentationLevel();

    // writing to disk. Return if there are still unsaved items left
    void flushSomeNodesToDisk(ForceBool force);
    void flushSomeNodesToDisk_callback(); // calls flush repeatedly, used as an asio callback
    std::string flushAll();
    int32_t saveLeaf(const UnspentOutput *uo);

    // session management.
    void commit(const UODBPrivate *priv);
    void rollback();

    // update m_changeCount
    void addChange(int count = 1);

    bool openInfo(int targetHeight);

    bool m_needsSave = false;
    std::atomic_int m_fileFull;

    // in-memory representation
    Streaming::BufferPool m_memBuffers;
    uint32_t m_jumptables[0x100000];
    mutable BucketMap m_buckets;
    std::atomic_int m_nextBucketIndex;
    std::atomic_int m_nextLeafIndex;

    // on-disk file.
    const boost::filesystem::path m_path;
    std::shared_ptr<char> m_buffer;
    Streaming::BufferPool m_writeBuffer;
    boost::iostreams::mapped_file m_file;

    // metadata not really part of the UTXO
    std::set<uint256> m_rejectedBlocks;

    /// wipes and creates a new datafile
    static DataFile *createDatafile(const boost::filesystem::path &filename, int firstBlockindex, const uint256 &firstHash);

    mutable std::recursive_mutex m_lock, m_saveLock;

    int m_initialBlockHeight = 0;
    int m_lastBlockHeight = 0;
    uint256 m_lastBlockHash;

    // Amount of inserts/deletes since last flush
    std::atomic_int m_changeCountBlock; // changes made that can't be saved yet.
    std::atomic_int m_changeCount; // changes that are waiting to be saved
    int m_changesSinceJumptableWritten = 0;
    int m_changesSincePrune = 0;
    int m_initialBucketSize = 0; // the size of the buckets-segment immediately after the last prune.
    boost::posix_time::ptime m_fragmentationCalcTimestamp;
    bool m_dbIsTip = false;
    int32_t m_fragmentationLevel = false;
    std::atomic_bool m_flushScheduled;

    // --- rollback info ---
    std::list<UnspentOutput*> m_leafsBackup; //< contains leafs deleted and never saved
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

    DataFile &operator=(const DataFile&o) = delete;
};

struct Limits
{
    uint32_t DBFileSize = 2147483600; // 2GiB
    int32_t FileFull = 1800000000; // 1.8GB
    uint32_t AutoFlush = 5000000; // every 5 million inserts/deletes, auto-flush jumptables
    int32_t ChangesToSave = 200000; // every 200K inserts/deletes, start a save-round.
};

class UODBPrivate
{
public:
    UODBPrivate(boost::asio::io_service &service,  const boost::filesystem::path &basedir, int beforeHeight = INT_MAX);

    // find existing DataFiles
    void init();

    boost::filesystem::path filepathForIndex(int fileIndex);
    DataFile *checkCapacity();

    boost::asio::io_service& ioService;

    bool memOnly = false; //< if true, we never flush to disk.
    bool doPrune = false;

    const boost::filesystem::path basedir;

    DataFileList dataFiles;

    static Limits limits;
};

#endif
