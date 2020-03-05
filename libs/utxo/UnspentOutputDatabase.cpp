/*
 * This file is part of the Flowee project
 * Copyright (C) 2018-2020 Tom Zander <tomz@freedommail.ch>
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
// #undef NDEBUG // uncomment to enable asserts even in release builds
#include "Pruner_p.h"
#include "UnspentOutputDatabase.h"
#include "UnspentOutputDatabase_p.h"
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>
#include <utils/hash.h>
#include <utils/utiltime.h>

#include <iostream>
#include <fstream>
#include <functional>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

// #define DEBUG_UTXO
#ifdef DEBUG_UTXO
# define DEBUGUTXO logCritical(Log::UTXO)
#else
# define DEBUGUTXO BCH_NO_DEBUG_MACRO()
#endif

// numbering in the .info files.
constexpr int MAX_INFO_NUM = 20;
constexpr int MAX_INFO_FILES = 13;

/*
 * Threading rules;
 * The connection between m_jumptables and m_buckets is via a unique Id
 * registered via m_nextBucketIndex (atomic)
 * We guarentee that 100% of the buckets are stored on disk periodically at which
 * point the atomic is reset to 1.
 * This means I can assume that a bucketId I find in the jumptables refers to a
 * unique bucket, even in a multi-threaded environemnt.
 */

Limits UODBPrivate::limits = Limits();

static std::uint32_t createShortHash(const uint256 &hash)
{
    auto txid = hash.begin();
    return (static_cast<uint32_t>(txid[0]) << 12) + (static_cast<uint32_t>(txid[1]) << 4) + ((static_cast<uint32_t>(txid[2]) & 0xF0) >> 4);
}

static bool matchesOutput(const Streaming::ConstBuffer &buffer, const uint256 &txid, int index)
{
    bool txidMatched = false, indexMatched = false;
    Streaming::MessageParser parser(buffer);
    bool separatorHit = false;
    while (!(indexMatched && txidMatched) && parser.next() == Streaming::FoundTag) {
        if (!txidMatched && parser.tag() == UODB::TXID) {
            if (parser.dataLength() == 32 && txid == parser.uint256Data())
                txidMatched = true;
            else if (parser.dataLength() == 24)
                txidMatched = memcmp(txid.begin() + 8, parser.bytesDataBuffer().begin(), 24) == 0;
            else
                return false;
        }
        else if (!indexMatched && !separatorHit && parser.tag() == UODB::OutIndex) {
            if (index == parser.intData())
                indexMatched = true;
            else
                return false;
        }
        else if (!indexMatched && parser.tag() == UODB::Separator) {
            indexMatched = 0 == index;
            separatorHit = true;
        }

        if (separatorHit && txidMatched)
            break;
    }
    return indexMatched && txidMatched;
}

//////////////////////////////////////////////////////////////

UnspentOutput::UnspentOutput(Streaming::BufferPool &pool, const uint256 &txid, int outIndex, int blockHeight, int offsetInBlock)
    : m_outIndex(outIndex),
      m_offsetInBlock(offsetInBlock),
      m_blockHeight(blockHeight)
{
    assert(outIndex >= 0);
    assert(blockHeight > 0);
    assert(offsetInBlock > 80);
    pool.reserve(55);
    Streaming::MessageBuilder builder(pool);
    builder.add(UODB::BlockHeight, blockHeight);
    builder.add(UODB::OffsetInBlock, offsetInBlock);
    builder.add(UODB::TXID, txid);
    if (outIndex != 0)
        builder.add(UODB::OutIndex, outIndex);
    builder.add(UODB::Separator, true);
    m_data = pool.commit();
}

UnspentOutput::UnspentOutput(uint64_t cheapHash, const Streaming::ConstBuffer &buffer)
    : m_data(buffer),
      m_outIndex(0),
      m_offsetInBlock(-1),
      m_blockHeight(-1),
      m_cheapHash(cheapHash)
{
    bool hitSeparator = false, foundUtxo = false;
    Streaming::MessageParser parser(m_data);
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == UODB::BlockHeight)
            m_blockHeight = parser.intData();
        else if (parser.tag() == UODB::OffsetInBlock)
            m_offsetInBlock = parser.intData();
        else if (!hitSeparator && parser.tag() == UODB::OutIndex)
            m_outIndex = parser.intData();
        else if (parser.tag() == UODB::TXID)
            foundUtxo = true;
        else if (parser.tag() == UODB::Separator)
            hitSeparator = true;

        if (hitSeparator && foundUtxo)
            break;
    }
    if (parser.next() == Streaming::Error)
        throw UTXOInternalError("Unparsable UTXO-record");
    assert(m_blockHeight > 0 && m_offsetInBlock >= 0);
}

uint256 UnspentOutput::prevTxId() const
{
    Streaming::MessageParser parser(m_data);
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == UODB::TXID) {
            if (parser.dataLength() == 32)
                return parser.uint256Data();
            else if (parser.dataLength() != 24)
                throw UTXOInternalError("TXID of wrong length");
            else { // pruned style, shorter hash, combine with our m_shortHash.
                char fullHash[32];
                WriteLE64(reinterpret_cast<unsigned char*>(fullHash), m_cheapHash);
                memcpy(fullHash + 8, parser.bytesData().data(), 24);
                return uint256(fullHash);
            }
        }
    }
    throw UTXOInternalError("No txid in UnspentOutput buffer found");
}

bool UnspentOutput::isCoinbase() const
{
    return m_offsetInBlock >= 81 && m_offsetInBlock < 90;
}


//////////////////////////////////////////////////////////////

UnspentOutputDatabase::UnspentOutputDatabase(boost::asio::io_service &service, const boost::filesystem::path &basedir)
    : d(new UODBPrivate(service, basedir))
{
}

UnspentOutputDatabase::UnspentOutputDatabase(UODBPrivate *priv)
    : d(priv)
{
}

UnspentOutputDatabase::~UnspentOutputDatabase()
{
    if (d->memOnly) {
        for (int i = 0; i < d->dataFiles.size(); ++i) {
            delete d->dataFiles.at(i);
        }
    }
    else {
        logCritical() << "Flushing UTXO cashes to disk...";
        bool m_changed = false;
        for (int i = 0; i < d->dataFiles.size() && !m_changed; ++i) {
            m_changed |= d->dataFiles.at(i)->m_needsSave;
        }
        for (int i = 0; i < d->dataFiles.size(); ++i) {
            auto df = d->dataFiles.at(i);
            DataFile::LockGuard deleteLock(df);
            deleteLock.deleteLater();
            if (m_changed) {
                std::lock_guard<std::recursive_mutex> saveLock(df->m_saveLock);
                std::lock_guard<std::recursive_mutex> lock2(df->m_lock);
                df->rollback();
                df->flushAll();
            }
        }
    }
    d->dataFiles.clear();
    fflush(nullptr);
    delete d;
}

UnspentOutputDatabase *UnspentOutputDatabase::createMemOnlyDB(const boost::filesystem::path &basedir)
{
    boost::asio::io_service ioService;
    auto d = new UODBPrivate(ioService, basedir);
    d->memOnly = true;
    return new UnspentOutputDatabase(d);
}

void UnspentOutputDatabase::setSmallLimits()
{
    UODBPrivate::limits.DBFileSize = 50000000;
    UODBPrivate::limits.FileFull = 30000000;
    UODBPrivate::limits.ChangesToSave = 50000;
}

void UnspentOutputDatabase::setChangeCountCausesStore(int count)
{
    assert(count > 1000);
    UODBPrivate::limits.ChangesToSave = count;
}

void UnspentOutputDatabase::insertAll(const UnspentOutputDatabase::BlockData &data)
{
    for (size_t i = 0; i < data.outputs.size(); i += 2000) {
        auto df = d->checkCapacity();
        df->insertAll(d, data, i, std::min(data.outputs.size(), i + 2000));
    }
}

void UnspentOutputDatabase::insert(const uint256 &txid, int outIndex, int blockHeight, int offsetInBlock)
{
    auto df = d->checkCapacity();
    df->insert(d, txid, outIndex, outIndex, blockHeight, offsetInBlock);
}

UnspentOutput UnspentOutputDatabase::find(const uint256 &txid, int index) const
{
    DataFileList dataFiles(d->dataFiles);
    for (int i = dataFiles.size(); i > 0; --i) {
        auto answer = dataFiles.at(i - 1)->find(txid, index);
        if (answer.isValid()) {
            answer.m_privData += (static_cast<uint64_t>(i) << 32);
            return answer;
        }
    }
    return UnspentOutput();
}

SpentOutput UnspentOutputDatabase::remove(const uint256 &txid, int index, uint64_t rmHint)
{
    SpentOutput done;
    const int32_t dbHint = static_cast<int32_t>((rmHint >> 32) & 0xFFFFFF);
    const uint32_t leafHint = rmHint & 0xFFFFFFFF;
    if (dbHint == 0) { // we don't know which one holds the data, which means we'll have to try all until we got a hit.
        DataFileList dataFiles(d->dataFiles);
        for (int i = dataFiles.size(); i > 0; --i) {
            done  = dataFiles.at(i - 1)->remove(d, txid, index, leafHint);
            if (done.isValid())
                break;
        }
    }
    else {
        assert(dbHint > 0);
        DataFileList dataFiles(d->dataFiles);
        if (dbHint > dataFiles.size())
            throw std::runtime_error("dbHint out of range");
        if (dbHint == dataFiles.size())
            d->checkCapacity();
        done = dataFiles.at(dbHint - 1)->remove(d, txid, index, leafHint);
    }
    return done;
}

void UnspentOutputDatabase::blockFinished(int blockheight, const uint256 &blockId)
{
    DEBUGUTXO << blockheight << blockId;
    int totalChanges = 0;

    for (int i = 0; i < d->dataFiles.size(); ++i) {
        DataFile* df = d->dataFiles.at(i);
        std::lock_guard<std::recursive_mutex> lock(df->m_lock);
        df->m_lastBlockHash = blockId;
        df->m_lastBlockHeight = blockheight;
        totalChanges += df->m_changesSinceJumptableWritten;
        df->commit(d);
        if (!d->memOnly && !df->m_dbIsTip) {
            /*
             * Avoid too great fragmentation by doing a garbage-collection (aka prune of dead records).
             *
             * The series of databases have 3 types of fragmentation.
             * The last one will be written until its full, possibly creating large fragmentation.
             * the one prior to that keeps the buckets close to the leafs.
             * For this one we can't really talk about fragmentation unless we check the actual buckets.
             * All DBs prior to that will have their buckets at the end and we can talk about fragmentation.
             */
            if (d->dataFiles.size() - 2 > i) // check all but the last two for fragmentation.
                d->doPrune = d->doPrune || df ->fragmentationLevel() > 60000000; // greater than 60MB
            else // use only changesSincePrune
                d->doPrune = d->doPrune || df->m_changesSincePrune > 800000;
        }
    }
    if (d->memOnly)
        return;

    d->checkCapacity();

    if (d->doPrune || totalChanges > 5000000) { // every 5 million inserts/deletes, auto-flush jumptables
        logCritical() << "Sha256 DB writing checkpoints" << d->basedir.string();
        std::vector<std::string> infoFilenames;
        for (int i = 0; i < d->dataFiles.size(); ++i) {
            DataFile *df = d->dataFiles.at(i);
            std::lock_guard<std::recursive_mutex> saveLock(df->m_saveLock);
            infoFilenames.push_back(df->flushAll());
            df->m_changesSinceJumptableWritten = 0;
        }

        if (d->doPrune && d->dataFiles.size() > 1) { // prune the DB files.
            d->doPrune = false;
            logCritical() << "Garbage-collecting the sha256-DB" << d->basedir.string();

            for (int db = 0; db < d->dataFiles.size() - 1; ++db) {
                DataFile* df = d->dataFiles.at(db);
                if (d->dataFiles.size() - 2 > db) {
                    if (df->fragmentationLevel() < 40000000) // not worth pruning, skip
                        continue;
                } else if (df->m_changesSincePrune < 200000) {
                    continue; // not worth pruning, skip
                }
                auto dbFilename = df->m_path;
                Pruner pruner(dbFilename.string() + ".db", infoFilenames.at(static_cast<size_t>(db)),
                              (db == d->dataFiles.size() - 2) ? Pruner::MostActiveDB : Pruner::OlderDB);

                logDebug() << "GC-ing file" << dbFilename.string() << infoFilenames.at(db);
                try {
                    pruner.prune();
                    DataFileCache cache(dbFilename.string());
                    for (int i = 0; i < MAX_INFO_NUM; ++i)
                        boost::filesystem::remove(cache.filenameFor(i));

                    DataFile::LockGuard delLock(d->dataFiles.at(db));
                    delLock.deleteLater();
                    pruner.commit();
                    const auto newDf = new DataFile(dbFilename);
                    newDf->m_initialBucketSize = pruner.bucketsSize();
                    d->dataFiles[db] = newDf;
                } catch (const std::runtime_error &failure) {
                    logCritical() << "Skipping GCing of db file" << db << "reason:" << failure;
                    pruner.cleanup();
                }
            }
            fflush(nullptr);
        }
    }
}

void UnspentOutputDatabase::rollback()
{
    DataFileList dataFiles(d->dataFiles);
    for (int i = 0; i < dataFiles.size(); ++i) {
        dataFiles.at(i)->rollback();
    }
}

void UnspentOutputDatabase::saveCaches()
{
    if (d->memOnly) return;

    auto dfs(d->dataFiles);
    for (int i = 0; i < dfs.size(); ++i) {
        DataFile *df = dfs.at(i);
        bool old = false;
        if (df->m_flushScheduled.compare_exchange_strong(old, true))
            d->ioService.post(std::bind(&DataFile::flushSomeNodesToDisk_callback, df));
    }
}

void UnspentOutputDatabase::setFailedBlockId(const uint256 &blockId)
{
    auto dfs(d->dataFiles);
    assert(dfs.size() > 0);
    auto df = dfs.last();
    std::lock_guard<std::recursive_mutex> lock(df->m_lock);
    const auto size = df->m_rejectedBlocks.size();
    df->m_rejectedBlocks.insert(blockId);
    if (size != df->m_rejectedBlocks.size())
        df->m_needsSave = true;
}

bool UnspentOutputDatabase::blockIdHasFailed(const uint256 &blockId) const
{
    auto dfs(d->dataFiles);
    assert(dfs.size() > 0);
    auto df = dfs.last();
    std::lock_guard<std::recursive_mutex> lock(df->m_lock);
    return df->m_rejectedBlocks.find(blockId) != df->m_rejectedBlocks.end();
}

bool UnspentOutputDatabase::loadOlderState()
{
    assert(d);
    assert(d->dataFiles.size() > 0);
    auto newD = new UODBPrivate(d->ioService, d->basedir, blockheight());
    newD->memOnly = d->memOnly;
    if (blockheight() == newD->dataFiles.last()->m_lastBlockHeight) {
        delete newD;
        return false;
    }
    delete d;
    d = newD;
    return true;
}

int UnspentOutputDatabase::blockheight() const
{
    return DataFileList(d->dataFiles).last()->m_lastBlockHeight;
}

uint256 UnspentOutputDatabase::blockId() const
{
    return DataFileList(d->dataFiles).last()->m_lastBlockHash;
}


// ///////////////////////////////////////////////////////////////////////
#ifdef linux
# include <sys/ioctl.h>
# include <linux/fs.h>
#endif

UODBPrivate::UODBPrivate(boost::asio::io_service &service, const boost::filesystem::path &basedir, int beforeHeight)
    : ioService(service),
      basedir(basedir)
{
    boost::filesystem::create_directories(basedir);
#ifdef linux
    // make sure that the dir we open up in has the "NO-CoW" flag set, in case this is
    // a btrfs filesystem. We are much slower when copy-on-write is enabled.
    FILE *fp = fopen(basedir.string().c_str(), "r");
    if (fp) {
        int flags;
        int rc = ioctl(fileno(fp), FS_IOC_GETFLAGS, &flags);
        if (rc == 0 && (flags & FS_NOCOW_FL) == 0) {
            flags |= FS_NOCOW_FL;
            ioctl(fileno(fp), FS_IOC_SETFLAGS, &flags); // ignore result, its Ok to fail.
        }
        fclose(fp);
    }
#endif
    int i = 1;
    while(true) {
        auto path = filepathForIndex(i);
        auto dbFile(path);
        dbFile.concat(".db");
        auto status = boost::filesystem::status(dbFile);
        if (status.type() != boost::filesystem::regular_file)
            break;

        dataFiles.append(new DataFile(path, beforeHeight));
        ++i;
    }
    if (dataFiles.size() > 1 && dataFiles.last()->m_lastBlockHeight == 0) {
        dataFiles.removeLast();
    }
    if (dataFiles.isEmpty()) {
        dataFiles.append(DataFile::createDatafile(filepathForIndex(1), 0, uint256()));
    }
    else {
        // find a checkpoint version all datafiles can agree on.
        bool allEqual = false;
        int tries = 0;
        while (!allEqual) {
            allEqual = true; // we assume they are until they are not.
            if (++tries > 9) {
                // can't find a state all databases rely on. This is a fatal problem.
                throw UTXOInternalError("Can't find a usable UTXO state");
            }
            int lastBlock = -1;
            uint256 lastBlockId;
            for (int i2 = 0; i2 < dataFiles.size(); ++i2) {
                DataFile *df = dataFiles.at(i2);
                if (lastBlock == -1) {
                    lastBlock = df->m_lastBlockHeight;
                    lastBlockId = df->m_lastBlockHash;
                } else if (lastBlock >= beforeHeight || lastBlock != df->m_lastBlockHeight || lastBlockId != df->m_lastBlockHash) {
                    allEqual = false;
                    int oldestHeight = std::min(lastBlock, df->m_lastBlockHeight);
                    oldestHeight = std::min(oldestHeight, beforeHeight - 1);
                    logCritical() << "Need to roll back to an older state:" << oldestHeight;
                    logDebug() << "First:" << lastBlock << lastBlockId << "datafile" << i2  << df->m_lastBlockHeight << df->m_lastBlockHash;
                    for (int i3 = 0; i3 < dataFiles.size(); ++i3) {
                        DataFile *dataFile = dataFiles.at(i3);
                        bool ok = dataFile->openInfo(oldestHeight);
                        if (!ok)
                            logWarning() << "finding the wanted block info file (height:" << oldestHeight << ") failed for"
                                         << df->m_path.string();
                    }
                    break;
                }
            }
        }
    }
    if (dataFiles.size() > 1) {
        auto lastFull = dataFiles.at(dataFiles.size() - 2);
        doPrune = lastFull->m_file.size() == limits.DBFileSize; // the original size
        if (doPrune)
            lastFull->m_changesSinceJumptableWritten = 5000000; // prune it sooner
    }
    dataFiles.last()->m_dbIsTip = true;
}

boost::filesystem::path UODBPrivate::filepathForIndex(int fileIndex)
{
    boost::filesystem::path answer = basedir;

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << "data-" << fileIndex;
    answer = answer / ss.str();
    return answer;
}

DataFile *UODBPrivate::checkCapacity()
{
    auto df = DataFileList(dataFiles).last();
    int fullValue = 1; // what the flush() method sets fileFull to
    const bool isFull = df->m_fileFull.compare_exchange_strong(fullValue, 2); // only true once after it was set to '1'
    if (!isFull)
        return df;

    doPrune = true;
    DEBUGUTXO << "Creating a new DataFile" << dataFiles.size();
    auto newDf = DataFile::createDatafile(filepathForIndex(dataFiles.size() + 1),
            df->m_lastBlockHeight, df->m_lastBlockHash);
    newDf->m_rejectedBlocks = df->m_rejectedBlocks;
    df->m_rejectedBlocks.clear();
    df->m_dbIsTip = false;
    dataFiles.append(newDf);
    return newDf;
}


//////////////////////////////////////////////////////////////////////////////////////////

static void nothing(const char *){}

DataFile::DataFile(const boost::filesystem::path &filename, int beforeHeight)
    :  m_fileFull(0),
      m_memBuffers(100000),
      m_nextBucketIndex(1),
      m_nextLeafIndex(1),
      m_path(filename),
      m_changeCountBlock(0),
      m_changeCount(0),
      m_fragmentationCalcTimestamp(boost::gregorian::date(1970,1,1)),
      m_flushScheduled(false),
      m_usageCount(1)
{
    memset(m_jumptables, 0, sizeof(m_jumptables));

    auto dbFile(filename);
    dbFile.concat(".db");
    m_file.open(dbFile, std::ios_base::binary | std::ios_base::in | std::ios_base::out);
    if (!m_file.is_open())
        throw UTXOInternalError("Failed to open UTXO DB file read/write");
    m_buffer = std::shared_ptr<char>(const_cast<char*>(m_file.const_data()), nothing);
    m_writeBuffer = Streaming::BufferPool(m_buffer, static_cast<int>(m_file.size()), true);

    DataFileCache cache(m_path);
    while (!cache.m_validInfoFiles.empty()) {
        auto iter = cache.m_validInfoFiles.begin();
        auto highest = iter;
        while (iter != cache.m_validInfoFiles.end()) {
            if (iter->lastBlockHeight > highest->lastBlockHeight && iter->lastBlockHeight < beforeHeight)
                highest = iter;
            ++iter;
        }

        if (cache.load(*highest, this))
            break; // all ok
        cache.m_validInfoFiles.erase(highest);
    }
}

void DataFile::insert(const UODBPrivate *priv, const uint256 &txid, int firstOutput, int lastOutput, int blockHeight, int offsetInBlock)
{
    assert(offsetInBlock > 80);
    assert(blockHeight > 0);
    assert(firstOutput >= 0);
    assert(lastOutput >= firstOutput);
    assert(!txid.IsNull());
    LockGuard delLock(this);
    const uint32_t shortHash = createShortHash(txid);
    uint32_t bucketId;
    {
        BucketHolder bucket;
        uint32_t lastCommittedBucketIndex;
        do {
            bucket.unlock();
            {
                std::lock_guard<std::recursive_mutex> lock(m_lock);
                lastCommittedBucketIndex = m_lastCommittedBucketIndex;
                bucketId = m_jumptables[shortHash];
                if (bucketId == 0) {// doesn't exist yet. Create now.
                    bucketId = static_cast<uint32_t>(m_nextBucketIndex.fetch_add(1));
                    DEBUGUTXO << "Insert leafs"  << txid << firstOutput << "-" << lastOutput << "creates new bucket id:" << bucketId;
                    bucket = m_buckets.lock(static_cast<int>(bucketId));
                    assert(*bucket == nullptr);
                    bucket.insertBucket(static_cast<int>(bucketId), Bucket());
                    m_jumptables[shortHash] = bucketId + MEMBIT;
                    break;
                }
            }
            if (bucketId < MEMBIT) // not in memory
                break;
            bucket = m_buckets.lock(static_cast<int>(bucketId & MEMMASK));
        } while (*bucket == nullptr);

        if (*bucket) {
            for (int i = firstOutput; i <= lastOutput; ++i) {
                const std::int32_t leafPos = m_nextLeafIndex.fetch_add(1);
                DEBUGUTXO << "Insert leaf"  << (leafPos & MEMMASK) << "shortHash:" << Log::Hex << shortHash;
                bucket->unspentOutputs.push_back(
                            OutputRef(txid.GetCheapHash(),
                                      static_cast<std::uint32_t>(leafPos) + MEMBIT,
                                      new UnspentOutput(m_memBuffers, txid, i, blockHeight, offsetInBlock)));
            }
            bucket->saveAttempt = 0;
            bucket.unlock();

            addChange(lastOutput - firstOutput + 1);

            if (bucketId > MEMMASK && (bucketId & MEMMASK) <= lastCommittedBucketIndex) {
                std::lock_guard<std::recursive_mutex> lock(m_lock);
                m_bucketsToNotSave.insert(bucketId);
            }
            return;
        }
        if (bucketId >= m_file.size()) // data corruption
            throw UTXOInternalError("Bucket points past end of file.");
    }

    // if we are still here that means that the bucket is stored on disk, we need to load it first.

    Bucket memBucket;
    assert(bucketId != 0);
    assert((bucketId & MEMBIT) == 0);

    // read from disk outside of the mutex, this is an expensive operation (because disk-io)
    memBucket.fillFromDisk(Streaming::ConstBuffer(m_buffer, m_buffer.get() + bucketId,
                                                  m_buffer.get() + m_file.size()),
                         static_cast<int>(bucketId));

    // after Disk-IO, acquire lock again.
    const int bucketIndex = m_nextBucketIndex.fetch_add(1);
    FlexLockGuard lock(m_lock);
    // re-fetch in case we had an AbA race
    bucketId = m_jumptables[shortHash];
    if ((bucketId & MEMBIT) || bucketId == 0) {// it got loaded into mem in parallel to our attempt
        lock.unlock();
        return insert(priv, txid, firstOutput, lastOutput, blockHeight, offsetInBlock);
    }

    m_committedBucketLocations.insert(std::make_pair(shortHash, bucketId));
    auto bucket = m_buckets.lock(bucketIndex);
    bucket.insertBucket(bucketIndex, std::move(memBucket));
    m_jumptables[shortHash] = static_cast<uint32_t>(bucketIndex) + MEMBIT;
    lock.unlock();

    for (int i = firstOutput; i <= lastOutput; ++i) {
        const std::int32_t leafPos = m_nextLeafIndex.fetch_add(1);
        DEBUGUTXO << "Insert leaf"  << (leafPos & MEMMASK) << "shortHash:" << Log::Hex << shortHash;
        DEBUGUTXO << Log::Hex << "  + from disk, bucketId:" << bucketIndex;

        bucket->unspentOutputs.push_back(
                    OutputRef(txid.GetCheapHash(),
                              static_cast<std::uint32_t>(leafPos) + MEMBIT,
                              new UnspentOutput(m_memBuffers, txid, i, blockHeight, offsetInBlock)));
    }
    bucket->saveAttempt = 0;
    bucket.unlock();
    addChange();
}

void DataFile::insertAll(const UODBPrivate *priv, const UnspentOutputDatabase::BlockData &data, size_t start, size_t end)
{
    for (size_t i = start; i < end; ++i) {
        assert(data.outputs.size() > i);
        const auto &o = data.outputs.at(i);
        insert(priv, o.txid, o.firstOutput, o.lastOutput, data.blockHeight, o.offsetInBlock);
    }
    int spaceLeft = UODBPrivate::limits.FileFull - m_writeBuffer.offset();
    if (m_changeCountBlock.load() * 120 > spaceLeft) {
        int notFull = 0; // only change if its still the default value.
        m_fileFull.compare_exchange_strong(notFull, 1);
        DEBUGUTXO << "insertAll: Marking file full";
    }
}

UnspentOutput DataFile::find(const uint256 &txid, int index) const
{
    LockGuard delLock(this);
    const uint32_t shortHash = createShortHash(txid);
    const auto cheapHash = txid.GetCheapHash();
    uint32_t bucketId;
    DEBUGUTXO << txid << index << Log::Hex << shortHash;
    BucketHolder bucketHolder;
    do {
        bucketHolder.unlock();
        {
            std::lock_guard<std::recursive_mutex> lock(m_lock);
            bucketId = m_jumptables[shortHash];
            if (bucketId == 0) // not found
                return UnspentOutput();
        }
        if (bucketId < MEMBIT) // not in memory
            break;
        bucketHolder = m_buckets.lock(static_cast<int>(bucketId & MEMMASK));
    } while (*bucketHolder == nullptr);

    Bucket bucket;
    if (*bucketHolder) {
        const Bucket *bucketRef = *bucketHolder;
        for (const OutputRef &ref : bucketRef->unspentOutputs) {
            if ((ref.leafPos & MEMBIT) && ref.cheapHash == cheapHash) {
                assert(ref.unspentOutput);
                if (matchesOutput(ref.unspentOutput->data(), txid, index)) {// found it!
                    UnspentOutput answer = *ref.unspentOutput;
                    answer.setRmHint(ref.leafPos);
                    return answer;
                }
            }
        }
        bucket.operator=(*bucketRef);
    }
    else if (bucketId >= m_file.size()) // disk based bucket, data corruption
        throw UTXOInternalError("Bucket points past end of file.");
    bucketHolder.unlock();

    if ((bucketId & MEMBIT) == 0) { // copy from disk
        // disk is immutable, so this is safe outside of the mutex.
        bucket.fillFromDisk(Streaming::ConstBuffer(m_buffer, m_buffer.get() + bucketId, m_buffer.get() + m_file.size()),
                               static_cast<std::int32_t>(bucketId));
        // FYI: a bucket coming from disk implies all leafs are also on disk.
    }

    // Only on-disk leafs to check, to do this fast we sort by position on disk for mem-locality.
    std::vector<uint32_t> diskRefs;
    for (auto ref : bucket.unspentOutputs) {
        if (!(ref.leafPos & MEMBIT) && ref.cheapHash == cheapHash)
            diskRefs.push_back(ref.leafPos);
    }
    std::sort(diskRefs.begin(), diskRefs.end());
    for (size_t i = diskRefs.size(); i > 0; --i) {
        const uint32_t pos = diskRefs.at(i - 1);
        // we do this all without any locking on a copy of the bucket because we know that stuff written to
        // m_buffer is immutable.
        Streaming::ConstBuffer buf(m_buffer, m_buffer.get() + pos, m_buffer.get() + m_file.size());
        if (matchesOutput(buf, txid, index)) { // found it!
            UnspentOutput answer(cheapHash, buf);
            answer.setRmHint(pos);
            return answer;
        }
    }

    return UnspentOutput();
}

SpentOutput DataFile::remove(const UODBPrivate *priv, const uint256 &txid, int index, uint32_t leafHint)
{
    LockGuard delLock(this);
    SpentOutput answer;
    const auto cheapHash = txid.GetCheapHash();
    const uint32_t shortHash = createShortHash(cheapHash);

    uint32_t bucketId;
    BucketHolder bucket;
    do {
        bucket.unlock();
        {
            std::lock_guard<std::recursive_mutex> lock(m_lock);
            bucketId = m_jumptables[shortHash];
            if (bucketId == 0) // not found
                return answer;
        }
        if (bucketId < MEMBIT) // not in memory
            break;
        bucket = m_buckets.lock(static_cast<int>(bucketId & MEMMASK));
    } while (*bucket== nullptr);

    Bucket memBucket;
    if (*bucket) {
        assert(!bucket->unspentOutputs.empty());
        DEBUGUTXO << "remove" << txid << index << "from bucket in memory. shortHash:" <<Log::Hex << shortHash;

        // first check the in-memory leafs if there is a hit.
        for (auto ref = bucket->unspentOutputs.begin(); ref != bucket->unspentOutputs.end(); ++ref) {
            if ((ref->leafPos & MEMBIT) && (ref->leafPos == leafHint || ref->cheapHash == cheapHash)) {
                UnspentOutput *output = ref->unspentOutput;
                if (ref->leafPos == leafHint || matchesOutput(output->data(), txid, index)) { // found it!
                    DEBUGUTXO << " +r " << txid << index << "removed, was in-mem leaf" << (ref->leafPos & MEMMASK);
                    answer.blockHeight = output->blockHeight();
                    answer.offsetInBlock = output->offsetInBlock();
                    assert(answer.isValid());
                    bucket->saveAttempt = 0;

                    const uint32_t leafIndex = ref->leafPos & MEMMASK; //copy as the next section frees ref

                    const bool deleteBucket = bucket->unspentOutputs.size() == 1;
                    if (deleteBucket)
                        bucket.deleteBucket(); // remove whole bucket if left empty
                    else
                        bucket->unspentOutputs.erase(ref);
                    bucket.unlock();
                    addChange();
                    std::lock_guard<std::recursive_mutex> lock(m_lock);
                    if (deleteBucket)
                        m_jumptables[shortHash] = 0;

                    if (leafIndex <= m_lastCommittedLeafIndex) {
                        // make backup of a leaf that has been committed but not yet saved
                        m_leafsBackup.push_back(output);
                    } else {
                        delete output;
                    }
                    // Mark bucket to not be saved. Bucket IDs with values higher than lastCommited don't get saved either way
                    if ((bucketId & MEMMASK) <= m_lastCommittedBucketIndex)
                        m_bucketsToNotSave.insert(bucketId);

                    return answer;
                }
            }
        }
        memBucket = **bucket; // maybe the hit is in an on-disk leaf (of this in-memory-bucket)
        bucket.unlock();
    }

    if (bucketId < MEMBIT) { // we could not find bucket in memory, read it from disk.
        // disk is immutable, so this is safe outside of the mutex.
        memBucket.fillFromDisk(Streaming::ConstBuffer(m_buffer, m_buffer.get() + bucketId, m_buffer.get() + m_file.size()),
                               static_cast<std::int32_t>(bucketId));
        // FYI: a bucket coming from disk implies all leafs are also on disk.
    }

    // Only on-disk leafs to check, to avoid unneeded disk-IO we sort by position-on-disk for locality.
    std::vector<uint32_t> diskRefs;
    bool hintFound = false;
    for (auto ref : memBucket.unspentOutputs) {
        if (ref.leafPos < MEMBIT && ref.cheapHash == cheapHash) {
            if (ref.leafPos == leafHint)
                hintFound = true;
            else
                diskRefs.push_back(ref.leafPos);
        }
    }
    std::sort(diskRefs.begin(), diskRefs.end());
    if (hintFound)
        diskRefs.insert(diskRefs.begin(), leafHint); // check the hint first.
    for (size_t i = diskRefs.size(); i > 0; --i) {
        const uint32_t pos = diskRefs.at(i - 1);
        // we do this all without any locking on a copy of the bucket because we know that stuff written to
        // m_buffer is immutable.
        Streaming::ConstBuffer buf(m_buffer, m_buffer.get() + pos, m_buffer.get() + m_file.size());
        if (matchesOutput(buf, txid, index)) { // found the leaf I want to remove!
            const OutputRef ref(cheapHash, pos);
            uint32_t newBucketId;
            do {
                bucket.unlock();
                {
                    std::lock_guard<std::recursive_mutex> lock(m_lock);
                    newBucketId = m_jumptables[shortHash];
                    if (newBucketId == 0) // since deleted
                        return answer;
                }
                if (newBucketId < MEMBIT) // not in memory
                    break;
                bucket = m_buckets.lock(static_cast<int>(newBucketId & MEMMASK));
            } while (*bucket == nullptr);

            if (*bucket) {
                DEBUGUTXO << "remove" << txid << index << "from (now) in-mem bucket, id:" << (newBucketId & MEMMASK)
                        << "leaf disk-pos:" << pos << "shortHash:" << Log::Hex << shortHash;
                bool found = false;
                for (auto iter = bucket->unspentOutputs.begin(); iter != bucket->unspentOutputs.end(); ++iter) {
                    if (*iter == ref) {
                        found = true;
                        bucket->unspentOutputs.erase(iter);
                        break;
                    }
                }
                if (!found)
                    return answer;

                if (bucket->unspentOutputs.empty()) { // remove if empty
                    bucket.deleteBucket();
                    bucket.unlock();
                    std::lock_guard<std::recursive_mutex> lock(m_lock);
                    m_jumptables[shortHash] = 0;
                } else {
                    bucket->saveAttempt = 0;
                    bucket.unlock();
                }
                std::lock_guard<std::recursive_mutex> lock(m_lock);
                if ((bucketId & MEMMASK) <= m_lastCommittedBucketIndex) {
                    m_bucketsToNotSave.insert(newBucketId);
                    m_leafIdsBackup.push_back(ref);
                }
            }
            else { // bucket not in memory (now). So it has to come from disk. (this is the very slow path)
                if (newBucketId != bucketId) {
                    DEBUGUTXO << "  +r reload bucket from disk";
                    // ugh, it got saved and probably changed. Load it again :(
                    memBucket.fillFromDisk(Streaming::ConstBuffer(m_buffer, m_buffer.get() + newBucketId,
                                                                  m_buffer.get() + m_file.size()),
                                       static_cast<std::int32_t>(newBucketId));
                }
                bool found = false; // detect double spend
                for (auto refIter = memBucket.unspentOutputs.begin(); refIter != memBucket.unspentOutputs.end(); ++refIter) {
                    if (*refIter == ref) {
                        memBucket.unspentOutputs.erase(refIter);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return answer;

                FlexLockGuard lock(m_lock);
                if (m_jumptables[shortHash] != newBucketId) {
                    // If this is the case then it got loaded into m_buckets in parallel.
                    // We can recurse and make the top of this method handle the removing then.
                    lock.unlock();
                    return remove(priv, txid, index, leafHint);
                }

                m_committedBucketLocations.insert(std::make_pair(shortHash, newBucketId));

                // We just loaded it from disk, should we insert the bucket into m_buckets?
                if (memBucket.unspentOutputs.empty()) { // no, just delete from jumptable
                    DEBUGUTXO << " +r bucket now empty, zero'd jumptable. Shorthash:" << Log::Hex <<shortHash;
                    m_jumptables[shortHash] = 0;
                } else {
                    DEBUGUTXO << " +r store bucket in mem. Bucket index:" << m_nextBucketIndex;
                    // Store in m_buckets (for saving) the now smaller bucket.
                    // Remember the unsaved on-disk position of the bucket for rollback()
                    const int bucketIndex = m_nextBucketIndex.fetch_add(1);
                    auto bucketHolder = m_buckets.lock(bucketIndex);
                    bucketHolder.insertBucket(bucketIndex, std::move(memBucket));
                    m_jumptables[shortHash] = static_cast<std::uint32_t>(bucketIndex) + MEMBIT;
                }
            }
            UnspentOutput uo(cheapHash, buf);
            answer.blockHeight = uo.blockHeight();
            answer.offsetInBlock = uo.offsetInBlock();
            assert(answer.isValid());

            addChange();
            break;
        }
    }
    return answer;
}

int DataFile::fragmentationLevel()
{
    const auto now = boost::posix_time::second_clock::universal_time();
    if ((now - m_fragmentationCalcTimestamp).total_seconds() < 100)
        return m_fragmentationLevel; // return cached

    m_fragmentationCalcTimestamp = now;
    uint32_t lowestOffset = m_file.size();
    uint32_t highestOffset = 0;

    for (int i = 0; i < 0x100000; ++i) {
        uint32_t bucketId = m_jumptables[i];
        if (bucketId < MEMBIT && bucketId > 0) {
            lowestOffset = std::min(lowestOffset, bucketId);
            highestOffset = std::max(highestOffset, bucketId);
        }
    }
    if (lowestOffset < highestOffset) {
        m_fragmentationLevel = highestOffset - lowestOffset;
        if (m_fragmentationLevel < m_initialBucketSize)
            m_fragmentationLevel = 0;
        else
            m_fragmentationLevel -= m_initialBucketSize;
        logDebug() << "Datafile" << m_path.string() << "fragmentation check" << m_fragmentationLevel
                   << "aka" << (m_fragmentationLevel / 1000000) << "MB";
    }
    return m_fragmentationLevel;
}

void DataFile::flushSomeNodesToDisk_callback()
{
    flushSomeNodesToDisk(NormalSave);
    m_flushScheduled = false;
}

void DataFile::flushSomeNodesToDisk(ForceBool force)
{
    LockGuard delLock(this);
    // in the rare case of flushAll() this may cause this method to be called from two
    // threads simultaniously. The below lock avoids this being an issue.
    std::lock_guard<std::recursive_mutex> saveLock(m_saveLock);

    uint32_t lastCommittedBucketIndex;
    std::set<uint32_t> bucketsToNotSave;
    {
        std::lock_guard<std::recursive_mutex> lock(m_lock);
        lastCommittedBucketIndex = m_lastCommittedBucketIndex;
        bucketsToNotSave = m_bucketsToNotSave;
    }
    const int changeCountAtStart = m_changeCount;
    int32_t flushedToDiskCount = 0;
    int32_t leafsFlushedToDisk = 0;
    std::list<SavedBucket> savedBuckets;
   /*
    * Iterate over m_buckets
    * if save counter is at 1, flush to disk unsaved leafs and update the bucket and the m_leafs
    * if save counter is >= 4, save bucket and make a copy of it. Don't delete it from m_buckets.
    * increase save count
    */
    for (auto iter = m_buckets.begin(); iter != m_buckets.end(); ++iter) {
        const uint32_t bucketId = static_cast<uint32_t>(iter.key());
        Bucket *bucket = &iter.value();
        assert(bucket);
        assert(!bucket->unspentOutputs.empty());

        bool allLeafsSaved = false;
        if (force == ForceSave || bucket->saveAttempt >= 1) {
            assert(!bucket->unspentOutputs.empty());
            // save any leafs not yet on disk
            allLeafsSaved = true;
            for (auto refIter = bucket->unspentOutputs.begin(); refIter != bucket->unspentOutputs.end(); ++refIter) {
                if (refIter->leafPos >= MEMBIT) {
                    if ((refIter->leafPos & MEMMASK) <= m_lastCommittedLeafIndex) {
                        assert(refIter->unspentOutput);
                        UnspentOutput *output = refIter->unspentOutput;
                        refIter->leafPos = static_cast<std::uint32_t>(saveLeaf(output));
                        refIter->unspentOutput = nullptr;
                        delete output;
                        leafsFlushedToDisk++;
                        assert((refIter->leafPos & MEMBIT) == 0);
                    } else {
                        assert(force == NormalSave);
                        allLeafsSaved = false;
                    }
                }
            }
        }
        if (allLeafsSaved && (force == ForceSave || bucket->saveAttempt >= 4)) {
            const bool saveBucket = bucketId <= lastCommittedBucketIndex
                    && bucketsToNotSave.find(bucketId + MEMBIT) == bucketsToNotSave.end();
            if (saveBucket) {
                flushedToDiskCount++;
                assert(!bucket->unspentOutputs.empty());
                uint32_t offset = static_cast<uint32_t>(bucket->saveToDisk(m_writeBuffer));
                assert(static_cast<uint32_t>(offset) < MEMBIT && offset >= 0);
                savedBuckets.push_back(SavedBucket(bucket->unspentOutputs, offset, bucket->saveAttempt));
                assert(!bucket->unspentOutputs.empty());
            }
        }
        ++bucket->saveAttempt;
    }
    flushedToDiskCount += leafsFlushedToDisk;
    if (flushedToDiskCount == 0)
        return;
    /*
    * Iterate over saved buckets and check if they are unchanged in m_buckets, if so then
    * update the now locked m_jumptable and delete it from m_buckets
    */
    for (const SavedBucket &savedBucket : savedBuckets) {
        assert(!savedBucket.unspentOutputs.empty());
        const auto shortHash = createShortHash(savedBucket.unspentOutputs.begin()->cheapHash);
        assert(shortHash < 0x100000);
        uint32_t bucketId;
        bool saveBucket;
        BucketHolder bucketHolder;
        do {
            bucketHolder.unlock();
            // get bucketId of transactions we just saved, notice that this may give a different
            // bucket than we just saved as the one we saved may have been deleted and a new one
            // (with a different bucketId) was created.
            {
                // to avoid a race condition where the jumptables and the m_buckets are out of sync
                // we unlock and try multiple times.
                std::lock_guard<std::recursive_mutex> lock(m_lock);
                bucketId = m_jumptables[shortHash];
                if (bucketId == 0) // deleted
                    saveBucket = false;
                else // check if we are still allowed to save
                    saveBucket = force == ForceSave || ((bucketId & MEMMASK) <= m_lastCommittedBucketIndex
                            && m_bucketsToNotSave.find(bucketId) == m_bucketsToNotSave.end());
            }
            if (bucketId < MEMBIT) {
                // race condition, lets not wait for the data to settle, just keep in mem
                // to save properly next flush
                saveBucket = false;
                break;
            }
            if (saveBucket)
                bucketHolder = m_buckets.lock(static_cast<int>(bucketId & MEMMASK));
        } while (saveBucket && *bucketHolder == nullptr);

        if (!saveBucket)
            continue;

        Bucket *bucket = *bucketHolder;
        assert(bucket);
        assert(!bucket->unspentOutputs.empty()); // the remove code should have removed empty buckets.

        if (bucket->unspentOutputs.size() != savedBucket.unspentOutputs.size()) // it got changed..
            continue;
        bool identical = true;
        for (size_t i = 0; identical && i < savedBucket.unspentOutputs.size(); ++i) {
            const OutputRef &savedItem = savedBucket.unspentOutputs.at(i);
            const OutputRef &item = bucket->unspentOutputs.at(i);
            identical = savedItem.leafPos == item.leafPos && savedItem.cheapHash == item.cheapHash;
        }

        if (identical) { // save was Ok and successful
            assert(savedBucket.offsetInFile < MEMBIT);
            bucketHolder.deleteBucket();
            bucketHolder.unlock();

            std::lock_guard<std::recursive_mutex> lock(m_lock);
            m_jumptables[shortHash] = savedBucket.offsetInFile;
        }
    }
    logInfo() << "Flushed" << flushedToDiskCount << "to disk." << m_path.filename().string() << "Filesize now:" << m_writeBuffer.offset();

    m_changeCount.fetch_sub(std::min(changeCountAtStart, flushedToDiskCount * 4));
    m_needsSave = true;
    if (m_writeBuffer.offset() > UODBPrivate::limits.FileFull) {
        int notFull = 0; // only change if its still the default value.
        m_fileFull.compare_exchange_strong(notFull, 1);
    }
    m_changesSinceJumptableWritten += flushedToDiskCount;
    m_changesSincePrune += flushedToDiskCount;
}

std::string DataFile::flushAll()
{
    LockGuard delLock(this);
    assert(m_bucketsToNotSave.empty());
    while (true) {
        /*
         * The jumptables and the buckets are updated separately which may
         * lead to they being out of sync. In all methods we take care to
         * avoid this causing issues, but the main effect here is that a
         * ForceSave may actually skip items because the buckets and the
         * jumptable don't agree at the time of saving.
         * Trying a second time with a bit of a wait will effectively solve this.
         */
        flushSomeNodesToDisk(ForceSave);
        if (m_buckets.begin() == m_buckets.end()) // no buckets left to save
            break;
        MilliSleep(10);
    }
#ifndef NDEBUG
    for (int i = 0; i < 0x100000; ++i) {
        assert(m_jumptables[i] < MEMBIT);
    }
#endif

    m_nextBucketIndex = 1;
    m_nextLeafIndex = 1;
    m_memBuffers.clear();
    commit(nullptr);

    DataFileCache cache(m_path);
    auto infoFilename = cache.writeInfoFile(this);
    m_needsSave = false;
    return infoFilename;
}

int32_t DataFile::saveLeaf(const UnspentOutput *uo)
{
    const int32_t offset = m_writeBuffer.offset();
    assert(uo->data().size() > 0);
    memcpy(m_writeBuffer.begin(), uo->data().begin(), static_cast<size_t>(uo->data().size()));
    m_writeBuffer.commit(uo->data().size());
    return offset;
}

void DataFile::commit(const UODBPrivate *priv)
{
    // mutex already locked by caller.
    const int nextBucketIndex = m_nextBucketIndex.load();
    assert(nextBucketIndex > 0);
    m_lastCommittedBucketIndex = static_cast<uint32_t>(nextBucketIndex) - 1;
    m_lastCommittedLeafIndex = static_cast<uint32_t>(m_nextLeafIndex.load()) - 1;
    for (UnspentOutput *output : m_leafsBackup) {
        delete output;
    }
    m_leafsBackup.clear();
    for (const OutputRef &ref : m_leafIdsBackup) {
        delete ref.unspentOutput;
    }
    m_leafIdsBackup.clear();
    m_bucketsToNotSave.clear();
    m_committedBucketLocations.clear();

    const int move = m_changeCountBlock.load();
    m_changeCountBlock.fetch_sub(move);
    m_changeCount.fetch_add(move);
    const int cc = m_changeCount.load();
    m_needsSave |= cc > 0;
    if (priv && !priv->memOnly && cc > UODBPrivate::limits.ChangesToSave) {
        if (m_flushScheduled && cc > UODBPrivate::limits.ChangesToSave * 2
                && move < UODBPrivate::limits.ChangesToSave) {
            // Saving is too slow! We are more than an entire chunk-size behind.
            // forcefully slow down adding data into memory.
            logInfo() << "saving too slow. Count:" << cc << "sleeping a little";
            boost::this_thread::sleep_for(boost::chrono::microseconds(std::min(cc, 100000)));
        }
        bool old = false;
        if (m_flushScheduled.compare_exchange_strong(old, true))
            priv->ioService.post(std::bind(&DataFile::flushSomeNodesToDisk_callback, this));
    }
}

void DataFile::rollback()
{
    LockGuard delLock(this);
    std::lock_guard<std::recursive_mutex> mutex_lock(m_lock);
    DEBUGUTXO << "Rollback" << m_path.string();
    // inserted new stuff is mostly irrelevant for rollback, we haven't been saving them,
    // all we need to do is remove them from memory.
    for (auto iter = m_buckets.begin(); iter != m_buckets.end();) {
#ifndef NDEBUG
      { const int bucketId = iter.key();
        assert(static_cast<uint32_t>(bucketId) <= MEMBIT);
        Bucket *bucket = &iter.value();
        assert(!bucket->unspentOutputs.empty());
        const auto shortHash = createShortHash(bucket->unspentOutputs.begin()->cheapHash);
        assert(shortHash < 0x100000);
        assert(m_jumptables[shortHash] >= MEMBIT);
        assert(m_jumptables[shortHash] == static_cast<uint32_t>(bucketId) + MEMBIT); }
#endif
        if (static_cast<uint32_t>(iter.key()) <= m_lastCommittedBucketIndex) {
            ++iter;
            continue;
        }
        assert (!iter.value().unspentOutputs.empty());
        const uint32_t shortHash = createShortHash(iter.value().unspentOutputs.front().cheapHash);
#ifndef NDEBUG
        DEBUGUTXO << "Rolling back adding a bucket" << iter.key() << "shortHash" << Log::Hex <<shortHash;
        for (const OutputRef &outRef: iter.value().unspentOutputs) {
            assert(createShortHash(outRef.cheapHash) == shortHash);
            if (outRef.leafPos > MEMBIT) {
                assert(outRef.unspentOutput);
                DEBUGUTXO << " + " << outRef.unspentOutput->prevTxId() << outRef.unspentOutput->outIndex();
            } else {
                DEBUGUTXO << " + saved leaf" << outRef.leafPos;
            }
        }
#endif
        // Newly inserted buckets can be totally new, or inserted because they were retrieved
        // from disk, changed and scheduled to be saved.
        // In case its new, just set the jumptable to zero. Otherwise fetch the previous
        // known on-disk position from m_committedJumptable
        uint32_t newBucketPos = 0;
        auto jti = m_committedBucketLocations.find(shortHash);
        if (jti != m_committedBucketLocations.end()) // before commit() it was a fully on-disk bucket.
            newBucketPos = jti->second;
        assert(newBucketPos >= 0);
        assert(newBucketPos < MEMBIT);
        if (newBucketPos > 0)
            DEBUGUTXO << " + Restoring old buckets disk pos" << newBucketPos << "shortHash" << Log::Hex <<shortHash;
        m_jumptables[shortHash] = newBucketPos;
        assert(iter.key() >= 0);
        for (const OutputRef &ref : iter.value().unspentOutputs) {
            delete ref.unspentOutput;
        }
        m_buckets.erase(iter);
    }
#ifndef NDEBUG
    // validate state of jumptable
    for (int i = 0; i < 0x100000; ++i) {
        uint32_t bucketId = m_jumptables[i];
        assert((bucketId < MEMBIT) || (bucketId & MEMMASK) <= m_lastCommittedBucketIndex);
        if (bucketId >= MEMBIT)
            assert(*m_buckets.lock(bucketId & MEMMASK));
    }
    for (auto iter = m_buckets.begin(); iter != m_buckets.end(); ++iter) {
        const int bucketId = iter.key();
        assert(static_cast<uint32_t>(bucketId) <= MEMBIT);
        Bucket *bucket = &iter.value();
        assert(!bucket->unspentOutputs.empty());
        const auto shortHash = createShortHash(bucket->unspentOutputs.begin()->cheapHash);
        assert(shortHash < 0x100000);
        assert(m_jumptables[shortHash] >= MEMBIT);
        assert(m_jumptables[shortHash] == static_cast<uint32_t>(bucketId) + MEMBIT);
    }
#endif

    for (auto jti = m_committedBucketLocations.begin(); jti != m_committedBucketLocations.end(); ++jti) {
        if (m_jumptables[jti->first] == 0) {
            DEBUGUTXO << "Restoring jumptable to on-disk bucket" << jti->first << jti->second;
            m_jumptables[jti->first] = jti->second;
        }
    }

    for (auto iter = m_buckets.begin(); iter != m_buckets.end(); ++iter) {
        Bucket &bucket = iter.value();
        uint32_t lastCommittedLeafIndex = m_lastCommittedLeafIndex | MEMBIT;
        for (auto refIter = bucket.unspentOutputs.begin(); refIter != bucket.unspentOutputs.end();) {
            if (refIter->leafPos > lastCommittedLeafIndex) {
                DEBUGUTXO << "Rolling back adding a leaf:" << (refIter->leafPos & MEMMASK)
                          << refIter->unspentOutput->prevTxId() << refIter->unspentOutput->outIndex()
                          << Log::Hex <<"shortHash";
                refIter = bucket.unspentOutputs.erase(refIter);
            }
            else {
                ++refIter;
            }
        }
    }

    for (auto leaf : m_leafsBackup) { // reinsert deleted leafs
        const uint256 txid = leaf->prevTxId();
        const uint32_t shortHash = createShortHash(txid);
        const std::int32_t leafPos = m_nextLeafIndex.fetch_add(1);
        DEBUGUTXO << "Rolling back removing a leaf:" << txid << leaf->outIndex() << "ShortHash:" << Log::Hex <<shortHash;

        // if the bucket exists, we add it. Otherwise we create a new bucket for this leaf.
        uint32_t bucketId = m_jumptables[shortHash];
        Bucket *bucket = nullptr;
        if (bucketId >= MEMBIT) { // add leaf
            BucketHolder bh = m_buckets.lock(static_cast<int32_t>(bucketId & MEMMASK));
            bucket = *bh;
            assert(bucket);
        } else { // bucket is not in memory
            DEBUGUTXO << " + reloading a bucket from disk for this";
            Bucket memBucket;
            memBucket.fillFromDisk(Streaming::ConstBuffer(m_buffer, m_buffer.get() + bucketId,
                                                          m_buffer.get() + m_file.size()),
                                 static_cast<int>(bucketId));
            const int bucketIndex = m_nextBucketIndex.fetch_add(1);
            m_jumptables[shortHash] = static_cast<uint32_t>(bucketIndex) + MEMBIT;
            BucketHolder bh = m_buckets.lock(bucketIndex);
            assert(*bh == nullptr);
            bh.insertBucket(bucketIndex, std::move(memBucket));
            bucket = *bh;
        }
        bucket->unspentOutputs.push_back(
                    OutputRef(txid.GetCheapHash(), static_cast<std::uint32_t>(leafPos) + MEMBIT, leaf));
        bucket->saveAttempt = 0;
    }

    for (auto outRef : m_leafIdsBackup) { // reinsert deleted leafs-ids (aka pos-on-disk for saved ones)
        const uint32_t shortHash = createShortHash(outRef.cheapHash);
        DEBUGUTXO << "Rolling back removing a leaf (from disk). pos:" << outRef.leafPos << "ShortHash:" << Log::Hex <<shortHash;

        // if the bucket exists, we add it. Otherwise we create a new bucket for this leaf.
        uint32_t bucketId = m_jumptables[shortHash];
        Bucket *bucket = nullptr;
        if (bucketId >= MEMBIT) { // add leaf
            BucketHolder bh = m_buckets.lock(static_cast<int32_t>(bucketId & MEMMASK));
            bucket = *bh;
            assert(bucket);
        } else { // bucket is not in memory
            DEBUGUTXO << " + reloading a bucket from disk for this";
            Bucket memBucket;
            memBucket.fillFromDisk(Streaming::ConstBuffer(m_buffer, m_buffer.get() + bucketId,
                                                          m_buffer.get() + m_file.size()),
                                 static_cast<int>(bucketId));
            const int bucketIndex = m_nextBucketIndex.fetch_add(1);
            m_jumptables[shortHash] = static_cast<uint32_t>(bucketIndex) + MEMBIT;
            BucketHolder bh = m_buckets.lock(bucketIndex);
            bh.insertBucket(bucketIndex, std::move(memBucket));
            bucket = *bh;
        }
        bucket->unspentOutputs.push_back(outRef);
        bucket->saveAttempt = 0;
    }

#ifndef NDEBUG
    // make sure that the newly inserted leafs are reachable
    for (auto leaf : m_leafsBackup) {
        const uint256 txid = leaf->prevTxId();
        const uint32_t shortHash = createShortHash(txid);
        assert(shortHash < 0x100000);
        assert (m_jumptables[shortHash]);
        uint32_t bucketId = m_jumptables[shortHash];
        assert (bucketId >= MEMBIT);
        BucketHolder bh = m_buckets.lock(bucketId & MEMMASK);
        assert(*bh);
        bool found = false;
        for (OutputRef rev : bh->unspentOutputs) {
            if (rev.leafPos > MEMBIT) {
                assert(rev.unspentOutput);
                found = rev.unspentOutput == leaf;
                if (found) break;
            }
        }
        assert (found);
    }
    // validate state of jumptable
    for (int i = 0; i < 0x100000; ++i) {
        uint32_t bucketId = m_jumptables[i];
        assert(bucketId < MEMBIT || static_cast<int>(bucketId & MEMMASK) < m_nextBucketIndex);
        if (bucketId >= MEMBIT) {
            assert(*m_buckets.lock(bucketId & MEMMASK));
        }
    }
    for (auto iter = m_buckets.begin(); iter != m_buckets.end(); ++iter) {
        const int bucketId = iter.key();
        assert(bucketId >= 0);
        assert(static_cast<uint32_t>(bucketId) <= MEMBIT);
        Bucket *bucket = &iter.value();
        assert(!bucket->unspentOutputs.empty());
        const auto shortHash = createShortHash(bucket->unspentOutputs.begin()->cheapHash);
        assert(shortHash < 0x100000);
        assert(m_jumptables[shortHash] >= MEMBIT);
        assert(m_jumptables[shortHash] == static_cast<uint32_t>(bucketId) + MEMBIT);
    }
#endif
    m_leafsBackup.clear();    // clear these as the pointers have moved ownership
    m_leafIdsBackup.clear();

    m_changeCountBlock.store(0);
    commit(nullptr);
}

void DataFile::addChange(int count)
{
    m_changeCountBlock.fetch_add(count);
}

bool DataFile::openInfo(int targetHeight)
{
    DataFileCache cache(m_path);
    DataFileCache::InfoFile candidate;
    for (auto info : cache.m_validInfoFiles) {
        if (info.lastBlockHeight <= targetHeight && info.lastBlockHeight > candidate.lastBlockHeight)
            candidate = info;
    }
    if (candidate.lastBlockHeight > 0)
        return cache.load(candidate, this);
    return false;
}

DataFile *DataFile::createDatafile(const boost::filesystem::path &filename, int firstBlockHeight, const uint256 &firstHash)
{
    auto dbFile = filename;
    dbFile.concat(".db");
    auto status = boost::filesystem::status(dbFile);
    if (status.type() != boost::filesystem::regular_file) {
        // doens't exist (yet)
        if (status.type() != boost::filesystem::file_not_found) {
            // removing non-file in its place. We don't delete directories, though. That sounds too dangerous.
            bool removed = boost::filesystem::remove(dbFile);
            if (!removed) {
                logFatal() << "Failed to create datafile, removing non-file failed";
                throw UTXOInternalError("Failed to replace non-file");
            }
        }
        // now create the file.
        assert(!filename.parent_path().string().empty());
        boost::filesystem::create_directories(filename.parent_path());
        boost::filesystem::ofstream file(dbFile);
        file.close();
        boost::filesystem::resize_file(dbFile, UODBPrivate::limits.DBFileSize);
    }

    DataFile *df = new DataFile(filename);
    df->m_initialBlockHeight = firstBlockHeight;
    df->m_lastBlockHeight = firstBlockHeight;
    df->m_lastBlockHash = firstHash;
    df->m_dbIsTip = true;
    return df;
}


/////////////////////////////////////////////////////////////////////////

DataFileCache::DataFileCache(const boost::filesystem::path &baseFilename)
    : m_baseFilename(baseFilename)
{
    for (int i = 1; i < MAX_INFO_NUM; ++i) {
        InfoFile info = parseInfoFile(i);
        if (info.initialBlockHeight >= 0) {
            m_validInfoFiles.push_back(info);
        }
    }
}

DataFileCache::InfoFile DataFileCache::parseInfoFile(int index) const
{
    assert(index >= 0);
    std::stringstream ss;
    ss << '.' << index << ".info";

    std::ifstream in(m_baseFilename.string() + ss.str(), std::ios::binary | std::ios::in);
    InfoFile answer;
    std::shared_ptr<char> buf(new char[32], std::default_delete<char[]>());
    answer.index = index;
    if (in.is_open()) {
        in.read(buf.get(), 32);
        Streaming::MessageParser parser(Streaming::ConstBuffer(buf, buf.get(), buf.get() + 32));
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == UODB::LastBlockHeight)
                answer.lastBlockHeight = parser.intData();
            else if (parser.tag() == UODB::FirstBlockHeight)
                answer.initialBlockHeight = parser.intData();
            else
                break;
        }
    }
    return answer;
}

std::string DataFileCache::writeInfoFile(DataFile *source)
{
    // if number of m_validInfoFiles are more than MAX_INFO_FILES
    // delete the one with the lowest / oldest 'lastBlockHeight'
    while (m_validInfoFiles.size() > MAX_INFO_FILES) {
        auto iter = m_validInfoFiles.begin();
        auto lowest = iter++;
        while (iter != m_validInfoFiles.end()) {
            if (iter->lastBlockHeight < lowest->lastBlockHeight)
                lowest = iter;
            ++iter;
        }
        boost::filesystem::remove(filenameFor(lowest->index));
        m_validInfoFiles.erase(lowest);
    }

    int newIndex = 1; // the index we use for the new info file
    for (auto i : m_validInfoFiles) {
        newIndex = std::max(newIndex, i.index);
    }
    if (++newIndex >= MAX_INFO_NUM) {
        newIndex = 1;
    }
    assert(newIndex > 0);
    assert(newIndex < MAX_INFO_NUM);

    boost::filesystem::remove(filenameFor(newIndex));
    std::string outFile = filenameFor(newIndex).string();
    std::ofstream out(outFile, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open())
        throw UTXOInternalError("Failed to open UTXO info file for writing");

    Streaming::MessageBuilder builder(Streaming::NoHeader, 256);
    builder.add(UODB::FirstBlockHeight, source->m_initialBlockHeight);
    builder.add(UODB::LastBlockHeight, source->m_lastBlockHeight);
    builder.add(UODB::LastBlockId, source->m_lastBlockHash);
    builder.add(UODB::PositionInFile, source->m_writeBuffer.offset());
    builder.add(UODB::ChangesSincePrune, source->m_changesSincePrune);
    if (source->m_initialBucketSize > 0)
        builder.add(UODB::InitialBucketSegmentSize, source->m_initialBucketSize);
    builder.add(UODB::IsTip, source->m_dbIsTip);
    if (source->m_dbIsTip) {
        for (auto blockId : source->m_rejectedBlocks) {
            builder.add(UODB::InvalidBlockHash, blockId);
        }
    }
    CHash256 ctx;
    ctx.Write(reinterpret_cast<const unsigned char*>(source->m_jumptables), sizeof(source->m_jumptables));
    uint256 result;
    ctx.Finalize(reinterpret_cast<unsigned char*>(&result));
    builder.add(UODB::JumpTableHash, result);
    builder.add(UODB::Separator, true);
    Streaming::ConstBuffer header = builder.buffer();
    out.write(header.constData(), header.size());
    out.write(reinterpret_cast<const char*>(source->m_jumptables), sizeof(source->m_jumptables));
    out.flush();

    return outFile;
}

bool DataFileCache::load(const DataFileCache::InfoFile &info, DataFile *target)
{
    logInfo() << "Loading" << filenameFor(info.index).string();
    assert(info.index >= 0);
    std::ifstream in(filenameFor(info.index).string(), std::ios::binary | std::ios::in);
    if (!in.is_open())
        return false;

    int posOfJumptable = 0;
    uint256 checksum;
    {
        std::shared_ptr<char> buf(new char[256], std::default_delete<char[]>());
        in.read(buf.get(), 256);
        Streaming::MessageParser parser(Streaming::ConstBuffer(buf, buf.get(), buf.get() + 256));
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == UODB::LastBlockHeight)
                target->m_lastBlockHeight = parser.intData();
            else if (parser.tag() == UODB::FirstBlockHeight)
                target->m_initialBlockHeight = parser.intData();
            else if (parser.tag() == UODB::LastBlockId)
                target->m_lastBlockHash = parser.uint256Data();
            else if (parser.tag() == UODB::JumpTableHash)
                checksum = parser.uint256Data();
            else if (parser.tag() == UODB::ChangesSincePrune)
                target->m_changesSincePrune = parser.intData();
            else if (parser.tag() == UODB::InitialBucketSegmentSize)
                target->m_initialBucketSize = parser.intData();
            else if (parser.tag() == UODB::PositionInFile) {
                target->m_writeBuffer = Streaming::BufferPool(target->m_buffer, static_cast<int>(target->m_file.size()), true);
                target->m_writeBuffer.markUsed(parser.intData());
                target->m_writeBuffer.forget(parser.intData());
            }
            else if (parser.tag() == UODB::InvalidBlockHash) {
                assert(parser.isByteArray() && parser.dataLength() == 32);
                target->m_rejectedBlocks.insert(parser.uint256Data());
            }
            else if (parser.tag() == UODB::Separator)
                break;
            else if (parser.tag() != UODB::IsTip) // isTip is purely for external tools, we don't trust that one.
                logDebug() << "UTOX info file has unrecognized tag" << parser.tag();
        }
        posOfJumptable = parser.consumed();
    }
    in.seekg(posOfJumptable);
    in.read(reinterpret_cast<char*>(target->m_jumptables), sizeof(target->m_jumptables));

    logDebug() << "Loaded" << filenameFor(info.index).string();
    logDebug() << "Block from" << target->m_initialBlockHeight << "to" << target->m_lastBlockHeight
                           << "changes since prune" << target->m_changesSincePrune;

    CHash256 ctx;
    ctx.Write(reinterpret_cast<const unsigned char*>(target->m_jumptables), sizeof(target->m_jumptables));
    uint256 result;
    ctx.Finalize(reinterpret_cast<unsigned char*>(&result));
    return result == checksum;
}

boost::filesystem::path DataFileCache::filenameFor(int index) const
{
    std::stringstream ss;
    ss << '.' << index << ".info";
    auto result (m_baseFilename);
    result.concat(ss.str());
    return result;
}
