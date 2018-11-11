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
#include "Pruner_p.h"
#include "UnspentOutputDatabase.h"
#include "UnspentOutputDatabase_p.h"
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>
#include <utils/util.h>
#include <server/hash.h>

#include <iostream>
#include <fstream>
#include <functional>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#define MEMBIT 0x80000000
#define MEMMASK 0x7FFFFFFF
#define SAVE_CHUNK_SIZE 50000

// #define DEBUG_UTXO
#ifdef DEBUG_UTXO
# define DEBUGUTXO logCritical(Log::UTXO)
#else
# define DEBUGUTXO BTC_NO_DEBUG_MACRO()
#endif

Limits UODBPrivate::limits = Limits();

static std::uint32_t createShortHash(const uint256 &hash)
{
    auto txid = hash.begin();
    return (static_cast<uint32_t>(txid[0]) << 12) + (static_cast<uint32_t>(txid[1]) << 4) + ((static_cast<uint32_t>(txid[2]) & 0xF0) >> 4);
}

static std::list<OutputRef>::iterator nextBucket(const std::list<OutputRef> &unsavedOutputs, const std::list<OutputRef>::iterator &begin)
{
    auto answer(begin);
    const std::uint32_t shortHash = createShortHash(begin->cheapHash);
    while (answer != unsavedOutputs.end()) {
        ++answer;
        if (createShortHash(answer->cheapHash) != shortHash)
            break;
    }
    return answer;
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
        throw std::runtime_error("Unparsable data");
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
                throw std::runtime_error("TXID of wrong length");
            else { // pruned style, shorter hash, combine with our m_shortHash.
                char fullHash[32];
                WriteLE64(reinterpret_cast<unsigned char*>(fullHash), m_cheapHash);
                memcpy(fullHash + 8, parser.bytesData().data(), 24);
                return uint256(fullHash);
            }
        }
    }
    throw std::runtime_error("No txid in UnspentOutput buffer found");
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
        for (int i = 0; i < d->dataFiles.size(); ++i) {
            auto df = d->dataFiles.at(i);
            DataFile::LockGuard deleteLock(df);
            deleteLock.deleteLater();
            std::lock_guard<std::recursive_mutex> saveLock(df->m_saveLock);
            std::lock_guard<std::recursive_mutex> lock2(df->m_lock);
            df->rollback();
            df->flushAll();
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
}

void UnspentOutputDatabase::insert(const uint256 &txid, int outIndex, int blockHeight, int offsetInBlock)
{
    d->dataFiles.last()->insert(d, txid, outIndex, blockHeight, offsetInBlock);
}

UnspentOutput UnspentOutputDatabase::find(const uint256 &txid, int index) const
{
    COWList<DataFile*> dataFiles(d->dataFiles);
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
    const size_t dbHint = (rmHint >> 32) & 0xFFFFFF;
    const uint32_t leafHint = rmHint & 0xFFFFFFFF;
    if (dbHint == 0) { // we don't know which one holds the data, which means we'll have to try all until we got a hit.
        COWList<DataFile*> dataFiles(d->dataFiles);
        for (int i = dataFiles.size(); i > 0; --i) {
            done  = dataFiles.at(i - 1)->remove(d, txid, index, leafHint);
            if (done.isValid())
                break;
        }
    }
    else {
        assert(dbHint > 0);
        if (dbHint > d->dataFiles.size())
            throw std::runtime_error("dbHint out of range");
        DataFile *df = d->dataFiles.at(dbHint - 1);
        done  = df->remove(d, txid, index, leafHint);
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
        df->commit();
    }

    if (d->dataFiles.last()->m_fileFull) {
        d->doPrune = true;
        DEBUGUTXO << "Creating a new DataFile" << d->dataFiles.size();
        auto df = d->dataFiles.last();
        d->dataFiles.append(DataFile::createDatafile(d->filepathForIndex(d->dataFiles.size() + 1),
                df->m_lastBlockHeight, df->m_lastBlockHash));
    }

    if (totalChanges > 5000000) { // every 5 million inserts/deletes, auto-flush jumptables
        std::vector<std::string> infoFilenames;
        for (int i = 0; i < d->dataFiles.size(); ++i) {
            DataFile *df = d->dataFiles.at(i);
            std::lock_guard<std::recursive_mutex> saveLock(df->m_saveLock);
            std::lock_guard<std::recursive_mutex> lock2(df->m_lock);
            infoFilenames.push_back(df->flushAll());
            df->m_changesSinceJumptableWritten = 0;
        }

        if (d->doPrune && d->dataFiles.size() > 1) { // prune the DB files.
            d->doPrune = false;
            logCritical() << "Pruning the UTXO";
            int db = d->dataFiles.size() - 2; // we skip the last DB file
            int jump = 1; // we don't do all DBs every time, this creates a nice sequence.
            do {
                auto dbFilename = d->dataFiles.at(db)->m_path;
                Pruner pruner(dbFilename.string() + ".db", infoFilenames.at(db),
                              jump == 1 ? Pruner::MostActiveDB : Pruner::OlderDB);
                try {
                    pruner.prune();
                    DataFileCache cache(dbFilename.string());
                    for (int i = 0; i < 10; ++i)
                        boost::filesystem::remove(cache.filenameFor(i));

                    DataFile::LockGuard lock(d->dataFiles.at(db));
                    lock.deleteLater();
                    pruner.commit();
                    d->dataFiles[db] = new DataFile(dbFilename);
                } catch (const std::runtime_error &pruneFailure) {
                    logCritical() << "Skipping pruning of db file" << db << "reason:" << pruneFailure;
                    pruner.cleanup();
                }

                db -= ++jump;
            } while (db >= 0);
            fflush(nullptr);
        }
    }
}

void UnspentOutputDatabase::rollback()
{
    COWList<DataFile*> dataFiles(d->dataFiles);
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
        std::lock_guard<std::recursive_mutex> lock(df->m_lock);
        if (df->m_flushScheduled)
            continue;
        df->m_flushScheduled = true;
        d->ioService.post(std::bind(&DataFile::flushSomeNodesToDisk, df, NormalSave));
    }
}

int UnspentOutputDatabase::blockheight() const
{
    return d->dataFiles.last()->m_lastBlockHeight;
}

uint256 UnspentOutputDatabase::blockId() const
{
    return d->dataFiles.last()->m_lastBlockHash;
}


// ///////////////////////////////////////////////////////////////////////

UODBPrivate::UODBPrivate(boost::asio::io_service &service, const boost::filesystem::path &basedir)
    : ioService(service),
      basedir(basedir)
{
    int i = 1;
    while(true) {
        auto path = filepathForIndex(i);
        auto dbFile(path);
        dbFile.concat(".db");
        auto status = boost::filesystem::status(dbFile);
        if (status.type() != boost::filesystem::regular_file)
            break;

        dataFiles.push_back(new DataFile(path));
        ++i;
    }
    if (dataFiles.empty()) {
        dataFiles.push_back(DataFile::createDatafile(filepathForIndex(1), 0, uint256()));
    } else {
        // find a checkpoint version all datafiles can agree on.
        bool allEqual = false;
        int tries = 0;
        while (!allEqual) {
            allEqual = true; // we assume they are until they are not.
            if (++tries > 9) {
                // can't find a state all databases rely on. This is a fatal problem.
                throw std::runtime_error("Can't find a usable UTXO state");
            }
            int lastBlock = -1;
            for (int i = 0; i < dataFiles.size(); ++i) {
                DataFile *df = dataFiles.at(i);
                if (lastBlock == -1) {
                    lastBlock = df->m_lastBlockHeight;
                } else if (lastBlock != df->m_lastBlockHeight) {
                    allEqual = false;
                    logCritical(Log::UTXO) << "Need to roll back to an older state:" << df->m_lastBlockHeight
                                           << "Where the first knew:" << lastBlock;
                    int oldestHeight = std::min(lastBlock, df->m_lastBlockHeight);
                    for (int i = 0; i < dataFiles.size(); ++i) {
                        DataFile *dataFile = dataFiles.at(i);
                        dataFile->openInfo(oldestHeight);
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
}

boost::filesystem::path UODBPrivate::filepathForIndex(int fileIndex)
{
    boost::filesystem::path answer = basedir;

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << "data-" << fileIndex;
    answer = answer / ss.str();
    return answer;
}


//////////////////////////////////////////////////////////////////////////////////////////

void Bucket::fillFromDisk(const Streaming::ConstBuffer &buffer, const int32_t bucketOffsetInFile)
{
    assert(bucketOffsetInFile >= 0);
    unspentOutputs.clear();
    Streaming::MessageParser parser(buffer);
    uint64_t cheaphash = 0;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == UODB::CheapHash) {
            cheaphash = parser.longData();
        }
        else if (parser.tag() == UODB::LeafPosRelToBucket) {
            int offset = parser.intData();
            if (offset > bucketOffsetInFile) {
                logFatal(Log::UTXO) << "Database corruption, offset to bucket messed up"
                                    << offset << bucketOffsetInFile;
                throw std::runtime_error("Database corruption, offset to bucket messed up");
            }
            unspentOutputs.push_back( {cheaphash,
                                       static_cast<std::uint32_t>(bucketOffsetInFile - offset)} );
        }
        else if (parser.tag() == UODB::LeafPosition) {
            unspentOutputs.push_back( {cheaphash, static_cast<std::uint32_t>(parser.intData())} );
        }
        else if (parser.tag() == UODB::LeafPosOn512MB) {
            unspentOutputs.push_back( {cheaphash, static_cast<std::uint32_t>(512 * 1024 * 1024 + parser.intData())} );
        }
        else if (parser.tag() == UODB::LeafPosFromPrevLeaf) {
            if (unspentOutputs.empty())
                throw std::runtime_error("Bucket referred to prev leaf while its the first");
            const int prevLeafPos = static_cast<int>(unspentOutputs.back().leafPos);
            const int newLeafPos = prevLeafPos - parser.intData();
            if (newLeafPos < 0)
                throw std::runtime_error("Invalid leaf pos due to LeafPosFroMPrevLeaf");
            unspentOutputs.push_back( {cheaphash, static_cast<std::uint32_t>(newLeafPos)} );
        }
        else if (parser.tag() == UODB::Separator) {
            return;
        }
    }
    throw std::runtime_error("Failed to parse bucket");
}

int32_t Bucket::saveToDisk(Streaming::BufferPool &pool) const
{
    const int32_t offset = pool.offset();

    Streaming::MessageBuilder builder(pool);
    uint64_t prevCH = 0;
    int prevPos = -1;
    for (auto item : unspentOutputs) {
        if (prevCH != item.cheapHash) {
            builder.add(UODB::CheapHash, item.cheapHash);
            prevCH = item.cheapHash;
        }

        /*
         * Figure out which tag to use.
         * To have smaller numbers and we save the one that occupies the lowest amount of bytes
         */
        assert(offset >= 0);
        assert((item.leafPos & MEMBIT) == 0);
        assert(item.leafPos < static_cast<std::uint32_t>(offset));
        const int leafPos = static_cast<int>(item.leafPos);
        UODB::MessageTags tagToUse = UODB::LeafPosition;
        int pos = leafPos;
        int byteCount = Streaming::serialisedIntSize(pos);

        // for values between 256MB and 768MB this moves 1 bit to the tag and avoids
        // the value from going from 4 bytes to 5 bytes.
        // notice that the negative sign is also strored outside the value bytes
        int m512TagSize = 20;
        if (leafPos >= 256 * 1024 * 1024)
            m512TagSize = Streaming::serialisedIntSize(leafPos - 512 * 1024 * 1024);
        if (m512TagSize < byteCount) {
            // store the distance to the 512MB file offset instead of from the start of file.
            tagToUse = UODB::LeafPosOn512MB;
            byteCount = m512TagSize;
            pos = leafPos - 512 * 1024 * 1024;
        }
        const int offsetFromBucketSize = Streaming::serialisedIntSize(offset - leafPos);
        if (offsetFromBucketSize < byteCount) {
            tagToUse = UODB::LeafPosRelToBucket;
            byteCount = offsetFromBucketSize;
            pos = offset - leafPos;
        }
        if (prevPos >= 0) {
            if (Streaming::serialisedIntSize(prevPos - leafPos) < byteCount) {
                tagToUse = UODB::LeafPosFromPrevLeaf;
                pos = prevPos - leafPos;
            }
        }
        builder.add(tagToUse, pos);
        prevPos = leafPos;
    }
    builder.add(UODB::Separator, true);
    pool.commit();
    return offset;
}


//////////////////////////////////////////////////////////////////////////////////////////

static void nothing(const char *){}

DataFile::DataFile(const boost::filesystem::path &filename)
    :  m_fileFull(0),
      m_memBuffers(100000),
      m_path(filename),
      m_usageCount(1)
{
    memset(m_jumptables, 0, sizeof(m_jumptables));

    auto dbFile(filename);
    dbFile.concat(".db");
    m_file.open(dbFile, std::ios_base::binary | std::ios_base::in | std::ios_base::out);
    if (!m_file.is_open())
        throw std::runtime_error("Failed to open file read/write");
    m_buffer = std::shared_ptr<char>(const_cast<char*>(m_file.const_data()), nothing);
    m_writeBuffer = Streaming::BufferPool(m_buffer, static_cast<int>(m_file.size()), true);

    DataFileCache cache(m_path);
    while (!cache.m_validInfoFiles.empty()) {
        auto iter = cache.m_validInfoFiles.begin();
        auto highest = iter;
        while (iter != cache.m_validInfoFiles.end()) {
            if (iter->lastBlockHeight > highest->lastBlockHeight)
                highest = iter;
            ++iter;
        }

        if (cache.load(*highest, this))
            break; // all ok
        cache.m_validInfoFiles.erase(highest);
    }
}

void DataFile::insert(const UODBPrivate *priv, const uint256 &txid, int outIndex, int blockHeight, int offsetInBlock)
{
    assert(offsetInBlock > 80);
    assert(blockHeight > 0);
    assert(outIndex >= 0);
    assert(!txid.IsNull());
    LockGuard lock(this);
    const uint32_t shortHash = createShortHash(txid);
    uint32_t bucketId;
    {
        std::lock_guard<std::recursive_mutex> lock(m_lock);
        if (!priv->memOnly && m_changeCount > SAVE_CHUNK_SIZE * 2) {
            // Saving is too slow! We are more than an entire chunk-size behind.
            // forcefully slow down adding data into memory.
            MilliSleep(m_changeCount / 1000);
        }
        bucketId = m_jumptables[shortHash];

        Bucket *bucket = nullptr;
        if (bucketId == 0) {
            bucketId = static_cast<uint32_t>(m_nextBucketIndex++);
            DEBUGUTXO << "Insert leaf"  << txid << outIndex << "creates new bucket id:" << bucketId;
            auto iterator = m_buckets.insert(std::make_pair(bucketId, Bucket())).first;
            bucket = &iterator->second;
            m_jumptables[shortHash] = bucketId + MEMBIT;
        } else if (bucketId & MEMBIT) { // highest bit is set. Bucket is in memory.
            bucket = &m_buckets.at(bucketId & MEMMASK);
            DEBUGUTXO << "Insert leaf"  << txid << outIndex << "into mem-bucket:" << (bucketId & MEMMASK);

            if ((bucketId & MEMMASK) <= m_lastCommittedBucketIndex)
                m_bucketsToNotSave.insert(bucketId);
        }
        if (bucket) {
            const std::int32_t leafPos = m_nextLeafIndex++;
            DEBUGUTXO << "Insert leaf"  << (leafPos & MEMMASK) << "shortHash:" << Log::Hex << shortHash;
            m_leafs.insert(std::make_pair(leafPos, UnspentOutput(m_memBuffers, txid, outIndex, blockHeight, offsetInBlock)));
            bucket->unspentOutputs.push_back({txid.GetCheapHash(), static_cast<std::uint32_t>(leafPos) + MEMBIT});
            bucket->saveAttempt = 0;
            addChange(priv);
            return;
        }
        if (bucketId >= m_file.size()) // data corruption
            throw std::runtime_error("Bucket points past end of file.");
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
    std::lock_guard<std::recursive_mutex> mutex_lock(m_lock);
    // re-fetch in case we had an AbA race
    bucketId = m_jumptables[shortHash];
    if ((bucketId & MEMBIT) || bucketId == 0) // it got loaded into mem in parallel to our attempt
        return insert(priv, txid, outIndex, blockHeight, offsetInBlock);

    m_committedBucketLocations.insert(std::make_pair(shortHash, bucketId));
    const auto iterator = m_buckets.insert(std::make_pair(m_nextBucketIndex, std::move(memBucket))).first;
    m_jumptables[shortHash] = static_cast<uint32_t>(m_nextBucketIndex) + MEMBIT;
    const std::int32_t leafPos = m_nextLeafIndex++;
    DEBUGUTXO << "Insert leaf"  << (leafPos & MEMMASK) << "shortHash:" << Log::Hex << shortHash;
    DEBUGUTXO << Log::Hex << "  + from disk, bucketId:" << m_nextBucketIndex;
    m_nextBucketIndex++;

    Bucket *bucket = &iterator->second;
    m_leafs.insert(std::make_pair(leafPos, UnspentOutput(m_memBuffers, txid, outIndex, blockHeight, offsetInBlock)));
    bucket->unspentOutputs.push_back({txid.GetCheapHash(), static_cast<std::uint32_t>(leafPos) + MEMBIT});
    bucket->saveAttempt = 0;

    addChange(priv);
}

UnspentOutput DataFile::find(const uint256 &txid, int index) const
{
    LockGuard lock(this);
    const uint32_t shortHash = createShortHash(txid);
    const auto cheapHash = txid.GetCheapHash();
    uint32_t bucketId;
    Bucket bucket;
    {
        std::lock_guard<std::recursive_mutex> lock(m_lock);
        bucketId = m_jumptables[shortHash];
        DEBUGUTXO << txid << index << Log::Hex << shortHash;;
        if (bucketId == 0) // not found
            return UnspentOutput();

        if (bucketId & MEMBIT) { // highest bit is set. Bucket is in memory.
            const Bucket *bucketRef = &m_buckets.at(bucketId & MEMMASK);
            for (auto ref : bucketRef->unspentOutputs) {
                if ((ref.leafPos & MEMBIT) && ref.cheapHash == cheapHash) {
                    const auto leafIter = m_leafs.find(ref.leafPos & MEMMASK);
                    assert(leafIter != m_leafs.end());
                    const UnspentOutput *output = &(leafIter->second);
                    if (matchesOutput(output->data(), txid, index)) {// found it!
                        UnspentOutput answer = *output;
                        answer.setRmHint(ref.leafPos);
                        return answer;
                    }
                }
            }
            bucket = *bucketRef; // copy to operate on after the lock is lifted
        }
        else if (bucketId >= m_file.size()) // disk based bucket, data corruption
            throw std::runtime_error("Bucket points past end of file.");
    }

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
    /*
     * Remove first finds the bucket that the item is (supposed to be) in.
     * and then it needs to iterate over all items in the bucket and do possibly
     * expensive lookups on disk to find out which is the actual full match.
     *
     * So to make this as fast as possible we first copy the bucket.
     * After this we drop the mutex lock.
     *
     * Then we sort the leafs we need to fetch from disk and fetch them in
     * sequence. The reason to sort is because we want to make sure we keep
     * reads from disk localized.
     *
     * As soon as we find the actual leafId to delete we can lock again and
     * re-fetch the bucket from the internal data and then delete the actual item.
     */
    LockGuard lock(this);
    SpentOutput answer;
    const auto cheapHash = txid.GetCheapHash();
    const uint32_t shortHash = createShortHash(cheapHash);

    Bucket memBucket;
    uint32_t bucketId;
    {
        std::lock_guard<std::recursive_mutex> lock(m_lock);
        bucketId = m_jumptables[shortHash];
        if (bucketId == 0) // not found
            return answer;

        if (!priv->memOnly && m_changeCount > SAVE_CHUNK_SIZE * 2) {
            // Saving is too slow! We are more than an entire chunk-size behind.
            // forcefully slow down adding data into memory.
            MilliSleep(m_changeCount / 1000);
        }
        if (bucketId & MEMBIT) { // highest bit is set. Bucket is in memory.
            auto bucketIter = m_buckets.find(bucketId & MEMMASK);
            assert(bucketIter != m_buckets.end());
            Bucket *bucket = &bucketIter->second;
            DEBUGUTXO << "remove" << txid << index << "from bucket in memory. shortHash:" <<Log::Hex << shortHash;

            // first check the in-memory items if there is a hit.
            for (auto ref : bucket->unspentOutputs) {
                if ((ref.leafPos & MEMBIT) && (ref.leafPos == leafHint || ref.cheapHash == cheapHash)) {
                    const auto leafIter = m_leafs.find(ref.leafPos & MEMMASK);
                    assert(leafIter != m_leafs.end());
                    UnspentOutput *output = &(leafIter->second);
                    if (ref.leafPos == leafHint || matchesOutput(output->data(), txid, index)) { // found it!
                        DEBUGUTXO << " +r " << txid << index << "removed, was in-mem leaf" << (ref.leafPos & MEMMASK);
                        if ((ref.leafPos & MEMMASK) <= m_lastCommittedLeafIndex) {
                            // make backup of a leaf that has been committed but not yet saved
                            m_leafsBackup.push_back(leafIter->second);
                        }
                        answer.blockHeight = output->blockHeight();
                        answer.offsetInBlock = output->offsetInBlock();
                        assert(answer.isValid());
                        m_leafs.erase(leafIter);

                        // Mark bucket to not be saved. Bucket IDs with values higher thn lastCommited don't get saved either way
                        if ((bucketId & MEMMASK) <= m_lastCommittedBucketIndex)
                            m_bucketsToNotSave.insert(bucketId);
                        bucket->unspentOutputs.remove(ref);
                        bucket->saveAttempt = 0;
                        if (bucket->unspentOutputs.empty()) { // remove if empty
                            m_buckets.erase(bucketIter);
                            m_jumptables[shortHash] = 0;
                        }
                        addChange(priv);
                        return answer;
                    }
                }
            }
            // make deep-copy before we drop the mutex
            memBucket = *bucket;
        }
    }

    if (bucketId < MEMBIT) { // we could not find it in memory, read it from disk.
        // disk is immutable, so this is safe outside of the mutex.
        memBucket.fillFromDisk(Streaming::ConstBuffer(m_buffer, m_buffer.get() + bucketId, m_buffer.get() + m_file.size()),
                               static_cast<std::int32_t>(bucketId));
        // FYI: a bucket coming from disk implies all leafs are also on disk.
    }

    // Only on-disk leafs to check, to do this fast we sort by position on disk for mem-locality.
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
        diskRefs.push_back(leafHint); // check the hint first.
    for (size_t i = diskRefs.size(); i > 0; --i) {
        const uint32_t pos = diskRefs.at(i - 1);
        // we do this all without any locking on a copy of the bucket because we know that stuff written to
        // m_buffer is immutable.
        Streaming::ConstBuffer buf(m_buffer, m_buffer.get() + pos, m_buffer.get() + m_file.size());
        if (matchesOutput(buf, txid, index)) { // found the leaf I want to remove!
            std::lock_guard<std::recursive_mutex> lock(m_lock);
            const OutputRef ref(cheapHash, pos);
            const uint32_t newBucketId = m_jumptables[shortHash];
            if (newBucketId & MEMBIT) {
                DEBUGUTXO << "remove" << txid << index << "from (now) in-mem bucket, id:" << (newBucketId & MEMMASK)
                        << "leaf disk-pos:" << pos << "shortHash:" << Log::Hex << shortHash;
                auto bucketIter = m_buckets.find(newBucketId & MEMMASK);
                assert(bucketIter != m_buckets.end());
                Bucket *bucket = &bucketIter->second;
                if ((bucketId & MEMMASK) <= m_lastCommittedBucketIndex) {
                    m_bucketsToNotSave.insert(newBucketId);
                    m_leafIdsBackup.push_back(ref);
                }

                // TODO detect double spend
                bucket->unspentOutputs.remove(ref);
                bucket->saveAttempt = 0;
                if (bucket->unspentOutputs.empty()) { // remove if empty
                    m_buckets.erase(bucketIter);
                    m_jumptables[shortHash] = 0;
                }
            }
            else { // bucket not in memory (now). So it has to come from disk.
                if (newBucketId != bucketId && (bucketId & MEMBIT)) {
                    DEBUGUTXO << "  +r reload bucket from disk";
                    // ugh, it got saved and maybe changed. Load it again :(
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
                m_committedBucketLocations.insert(std::make_pair(shortHash, newBucketId));

                // We just loaded it from disk, should we insert the bucket into m_buckets?
                if (memBucket.unspentOutputs.empty()) { // no, just delete from jumptable
                    DEBUGUTXO << " +r bucket now empty, zero'd jumptable. Shorthash:" << Log::Hex <<shortHash;
                    m_jumptables[shortHash] = 0;
                } else {
                    DEBUGUTXO << " +r store bucket in mem. Bucket index:" << m_nextBucketIndex;
                    // Store in m_buckets (for saving) the now smaller bucket.
                    // Remember the unsaved on-disk position of the bucket for rollback()
                    m_buckets.insert(std::make_pair(m_nextBucketIndex, memBucket));
                    m_jumptables[shortHash] = static_cast<std::uint32_t>(m_nextBucketIndex++) + MEMBIT;
                }
            }
            UnspentOutput uo(cheapHash, buf);
            answer.blockHeight = uo.blockHeight();
            answer.offsetInBlock = uo.offsetInBlock();
            assert(answer.isValid());

            addChange(priv);
            return answer;
        }
    }

    return answer;
}

bool DataFile::flushSomeNodesToDisk(ForceBool force)
{
    LockGuard lock(this);
    // in the rare case of flushAll() this may cause this method to be called from two
    // threads simultaniously. The below lock avoids this being an issue.
    std::lock_guard<std::recursive_mutex> saveLock(m_saveLock);

    logInfo(Log::UTXO) << "Flush nodes starting" << m_path.filename().string();
    std::list<OutputRef> unsavedOutputs;
    std::unordered_map<int, UnspentOutput> leafs;
    std::set<uint32_t> bucketsToSave;
    // first gather the stuff we want to save, we need the mutex as this is stored in various containers
    {
        std::lock_guard<std::recursive_mutex> lock(m_lock);
        logInfo(Log::UTXO) << " += Leafs in mem:" << m_leafs.size() << "buckets in mem:" << m_buckets.size();
        leafs = m_leafs;

        // Collect buckets (at least their content) we are going to store to disk.
        for (auto iter = m_buckets.begin(); iter != m_buckets.end(); ++iter) {
            const int bucketId = iter->first;
            Bucket *bucket = &iter->second;
            assert(!bucket->unspentOutputs.empty());

            const auto shortHash = createShortHash(bucket->unspentOutputs.begin()->cheapHash);
            assert(shortHash < 0x100000);
            assert(m_jumptables[shortHash] >= MEMBIT);

            // we are always allowed to save the content of the bucket, but the bucket itself we only save
            // after we got a commit of that item.
            bool saveBucket = bucketId <= m_lastCommittedBucketIndex
                    && m_bucketsToNotSave.find(bucketId + MEMBIT) == m_bucketsToNotSave.end();

            // additionally we wait a while after modificaton of a bucket. Seen in 'saveAttempt'
            if (saveBucket && force != ForceSave && ++bucket->saveAttempt < 1)
                saveBucket = false;

            if (saveBucket) {
                // copy the whole bucket
                bucketsToSave.insert(createShortHash(bucket->unspentOutputs.begin()->cheapHash));
                unsavedOutputs.insert(unsavedOutputs.end(), bucket->unspentOutputs.begin(), bucket->unspentOutputs.end());
            } else {
                // only copy the leafs that need saving.
                for (auto leaf : bucket->unspentOutputs) {
                    if (leaf.leafPos >= MEMBIT && (leaf.leafPos & MEMBIT) <= m_lastCommittedLeafIndex) {
                        unsavedOutputs.insert(unsavedOutputs.end(), leaf);
                    }
                }
            }
            if (unsavedOutputs.size() > SAVE_CHUNK_SIZE * 5)
                break;
        }
    }
    if (unsavedOutputs.empty()) {
        m_flushScheduled = false;
        m_changeCount = 0;
        return false;
    }

    uint32_t flushedToDiskCount = 0;
    /*
     * From one long list we can split it into buckets again using nextBucket() which
     * uses the fact that in a bucket all items use the same shortHash.
     */
    auto begin = unsavedOutputs.begin();
    auto end = nextBucket(unsavedOutputs, begin);
    std::map<uint32_t, uint32_t> bucketOffsets; // from shortHash to new offset-in-file
    std::map<uint32_t, uint32_t> leafOffsets; // from leafPos to new offset-in-file
    do {
        Bucket updatedBucket;
        const auto shortHash = createShortHash(begin->cheapHash);
        uint32_t leafsFlushedToDisk = 0;
        while (begin != end) {
            updatedBucket.unspentOutputs.push_back(*begin);
            if (begin->leafPos & MEMBIT) { // save leaf and update temp bucket
                auto leaf = leafs.find(begin->leafPos & MEMMASK);
                assert(leaf != leafs.end());
                const std::uint32_t offset = static_cast<std::uint32_t>(saveLeaf(leaf->second));
                leafsFlushedToDisk++;
                assert((offset & MEMBIT) == 0);
                leafOffsets.insert(std::make_pair(begin->leafPos, offset)); // remember new offset to update real bucket
                updatedBucket.unspentOutputs.back().leafPos = offset;
            }
            assert((updatedBucket.unspentOutputs.back().leafPos & MEMBIT) == 0);
            ++begin;
        }
        flushedToDiskCount += leafsFlushedToDisk;

        if (bucketsToSave.find(shortHash) != bucketsToSave.end()) {
            flushedToDiskCount++;
            auto offset = updatedBucket.saveToDisk(m_writeBuffer);
            assert(static_cast<uint32_t>(offset) < MEMBIT && offset >= 0);
            bucketOffsets.insert(std::make_pair(shortHash, offset));
        }

        end = nextBucket(unsavedOutputs, begin);
    }
    while (end != unsavedOutputs.end());

    // lock again and update inner structures as fast as possible
    std::lock_guard<std::recursive_mutex> mutex_lock(m_lock);
    DEBUGUTXO << " +~ Leafs in mem:" << m_leafs.size() << "buckets in mem:" << m_buckets.size();
    begin = unsavedOutputs.begin();
    end = nextBucket(unsavedOutputs, begin);
    do {
        const auto shortHash = createShortHash(begin->cheapHash);
        assert(shortHash < 0x100000);
        const uint32_t bucketId = m_jumptables[shortHash];
        if (bucketId == 0) {
            // then the bucket and all its contents has been removed already in parallel to our saving it
            begin = end;
            end = nextBucket(unsavedOutputs, begin);
            continue; // next bucket
        }
        bool eraseBucket = bucketsToSave.find(shortHash) != bucketsToSave.end();

        assert(bucketId >= MEMBIT); // this method is the only one that can save
        auto bucketIter = m_buckets.find(bucketId & MEMMASK);
        assert(bucketIter != m_buckets.end());
        Bucket *bucket = &bucketIter->second;
        assert(bucket);
        assert(!bucket->unspentOutputs.empty()); // the remove code should have removed empty buckets.
        auto inMemOutputsIter = bucket->unspentOutputs.begin();

        // for each leaf. Remove from memory and update pointers to saved version.
        while (begin != end) {
            eraseBucket = eraseBucket && inMemOutputsIter != bucket->unspentOutputs.end()
                    && *inMemOutputsIter == *begin;
            auto newOffset = leafOffsets.find(begin->leafPos);
            if (newOffset != leafOffsets.end()) {
                auto oldLeaf = m_leafs.find(begin->leafPos & MEMMASK);
                if (oldLeaf != m_leafs.end()) {
                    // DEBUGUTXO << "remove leaf" << Log::Hex << begin->leafPos;
                    m_leafs.erase(oldLeaf);
                    for (auto iter = bucket->unspentOutputs.begin(); iter != bucket->unspentOutputs.end(); ++iter) {
                        if (iter->leafPos == begin->leafPos) {
                            // DEBUGUTXO << Log::Hex << " + === changing to" << newOffset->second << " for bucket" << Log::Dec << bucketId;
                            iter->leafPos = newOffset->second;
                            break;
                        }
                    }
                }
            }
            // if this leaf didn't get saved, then we won't save the bucket.
            eraseBucket = eraseBucket && inMemOutputsIter->leafPos < MEMBIT;

            ++begin;
            ++inMemOutputsIter;
        }

        if (eraseBucket && inMemOutputsIter == bucket->unspentOutputs.end()) {
            const auto savedOffset = bucketOffsets.find(shortHash); // only present if we decided to save it
            if (savedOffset != bucketOffsets.end()) {
                // replace pointers to use the saved bucket.
                assert(savedOffset->second < MEMBIT);
                m_jumptables[shortHash] = savedOffset->second;
                m_buckets.erase(bucketIter);
            }
        }
        end = nextBucket(unsavedOutputs, begin);
    }
    while (end != unsavedOutputs.end());

    logInfo(Log::UTXO) << "Flushed" << flushedToDiskCount << "to disk. Filesize now:" << m_writeBuffer.offset();
    logInfo(Log::UTXO) << " +- Leafs in mem:" << m_leafs.size() << "Buckets in mem:" << m_buckets.size();
    m_flushScheduled = false;
    m_changeCount = m_leafs.size();

    m_jumptableNeedsSave = true;
    if (!m_fileFull && m_writeBuffer.offset() > UODBPrivate::limits.FileFull) {
        m_fileFull = true;
    }
    m_changesSinceJumptableWritten += flushedToDiskCount;

    return !m_leafs.empty() || !m_buckets.empty();
}

std::string DataFile::flushAll()
{
    LockGuard lock(this);
    // mutex already locked by caller.
    // save all, the UnspentOutputDatabase has a lock that makes this a stop-the-world event
    while (flushSomeNodesToDisk(ForceSave));
    assert(m_buckets.size() == 0);
    assert(m_leafs.size() == 0);
#ifndef NDEBUG
    for (int i = 0; i < 0x100000; ++i) {
        assert(m_jumptables[i] < MEMBIT);
    }
#endif

    m_nextBucketIndex = 0;
    m_buckets.clear();
    m_nextLeafIndex = 0;
    m_leafs.clear();
    m_memBuffers.clear();
    commit();

    DataFileCache cache(m_path);
    auto infoFilename = cache.writeInfoFile(this);
    m_jumptableNeedsSave = false;
    return infoFilename;
}

int32_t DataFile::saveLeaf(const UnspentOutput &uo)
{
    const int32_t offset = m_writeBuffer.offset();
    assert(uo.data().size() > 0);
    memcpy(m_writeBuffer.begin(), uo.data().begin(), static_cast<size_t>(uo.data().size()));
    m_writeBuffer.commit(uo.data().size());
    return offset;
}

void DataFile::commit()
{
    // mutex already locked by caller.
    m_lastCommittedBucketIndex = m_nextBucketIndex - 1;
    m_lastCommittedLeafIndex = m_nextLeafIndex - 1;
    m_leafsBackup.clear();
    m_leafIdsBackup.clear();
    m_bucketsToNotSave.clear();
    m_committedBucketLocations.clear();
}

void DataFile::rollback()
{
    LockGuard lock(this);
    std::lock_guard<std::recursive_mutex> mutex_lock(m_lock);
    DEBUGUTXO << "Rollback" << m_path.string();
    // inserted new stuff is mostly irrelevant for rollback, we haven't been saving them,
    // all we need to do is remove them from memory.
    for (auto iter = m_buckets.begin(); iter != m_buckets.end();) {
#ifndef NDEBUG
      { const int bucketId = iter->first;
        assert(static_cast<uint32_t>(bucketId) <= MEMBIT);
        Bucket *bucket = &iter->second;
        assert(!bucket->unspentOutputs.empty());
        const auto shortHash = createShortHash(bucket->unspentOutputs.begin()->cheapHash);
        assert(shortHash < 0x100000);
        assert(m_jumptables[shortHash] >= MEMBIT);
        assert(m_jumptables[shortHash] == static_cast<uint32_t>(bucketId) + MEMBIT); }
#endif
        if (iter->first <= m_lastCommittedBucketIndex) {
            ++iter;
            continue;
        }
        assert (!iter->second.unspentOutputs.empty());
        const uint32_t shortHash = createShortHash(iter->second.unspentOutputs.front().cheapHash);
#ifndef NDEBUG
        DEBUGUTXO << "Rolling back adding a bucket" << iter->first << "shortHash" << Log::Hex <<shortHash;
        for (const OutputRef &outRef: iter->second.unspentOutputs) {
            assert(createShortHash(outRef.cheapHash) == shortHash);
            if (outRef.leafPos > MEMBIT) {
                UnspentOutput leaf = m_leafs.at(outRef.leafPos & MEMMASK);
                DEBUGUTXO << " + " << leaf.prevTxId() << leaf.outIndex();
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
        assert(iter->first >= 0);
        iter = m_buckets.erase(iter);
    }
#ifndef NDEBUG
    // validate state of jumptable
    for (int i = 0; i < 0x100000; ++i) {
        uint32_t bucketId = m_jumptables[i];
        assert((bucketId < MEMBIT) || static_cast<int>(bucketId & MEMMASK) <= m_lastCommittedBucketIndex);
        if (bucketId >= MEMBIT) {
            assert (m_buckets.find(bucketId & MEMMASK) != m_buckets.end());
        }
    }
    for (auto iter = m_buckets.begin(); iter != m_buckets.end(); ++iter) {
        const int bucketId = iter->first;
        assert(static_cast<uint32_t>(bucketId) <= MEMBIT);
        Bucket *bucket = &iter->second;
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

    for (auto iter = m_leafs.begin(); iter != m_leafs.end();) {
        if (iter->first > m_lastCommittedLeafIndex) {
            // also remove reference from bucket to this leaf
            const uint64_t cheapHash = iter->second.prevTxId().GetCheapHash();
            const uint32_t shortHash = createShortHash(cheapHash);
            DEBUGUTXO << "Rolling back adding a leaf:" << iter->first << iter->second.prevTxId() << iter->second.outIndex() << Log::Hex <<"shortHash" << shortHash;
            uint32_t bucketId = m_jumptables[shortHash];
            if (bucketId >= MEMBIT) {
                DEBUGUTXO << " + also removing from in-memory bucket";
                auto bucketIter = m_buckets.find(static_cast<int>(bucketId & MEMMASK));
                assert(bucketIter != m_buckets.end());
                bucketIter->second.unspentOutputs.remove(OutputRef(cheapHash, iter->first + MEMBIT));
            }
            iter = m_leafs.erase(iter);
        } else {
            ++iter;
        }
    }

    for (auto leaf : m_leafsBackup) { // reinsert deleted leafs
        const uint256 txid = leaf.prevTxId();
        const uint32_t shortHash = createShortHash(txid);
        const std::int32_t leafPos = m_nextLeafIndex++;
        m_leafs.insert(std::make_pair(leafPos, leaf));
        DEBUGUTXO << "Rolling back removing a leaf:" << txid << leaf.outIndex() << "ShortHash:" << Log::Hex <<shortHash;

        // if the bucket exists, we add it. Otherwise we create a new bucket for this leaf.
        uint32_t bucketId = m_jumptables[shortHash];
        Bucket *bucket = nullptr;
        if (bucketId >= MEMBIT) { // add leaf
            auto iter = m_buckets.find(static_cast<int32_t>(bucketId & MEMMASK));
            assert(iter != m_buckets.end());
            bucket = &iter->second;
        } else { // bucket is not in memory
            DEBUGUTXO << " + reloading a bucket from disk for this";
            Bucket memBucket;
            memBucket.fillFromDisk(Streaming::ConstBuffer(m_buffer, m_buffer.get() + bucketId,
                                                          m_buffer.get() + m_file.size()),
                                 static_cast<int>(bucketId));
            m_jumptables[shortHash] = static_cast<uint32_t>(m_nextBucketIndex) + MEMBIT;
            auto iterator = m_buckets.insert(std::make_pair(m_nextBucketIndex++, std::move(memBucket))).first;
            bucket = &iterator->second;
        }
        bucket->unspentOutputs.push_back({txid.GetCheapHash(), static_cast<std::uint32_t>(leafPos) + MEMBIT});
        bucket->saveAttempt = 0;
    }

    for (auto outRef : m_leafIdsBackup) { // reinsert deleted leafs-ids (pos-on-disk
        const uint32_t shortHash = createShortHash(outRef.cheapHash);
        DEBUGUTXO << "Rolling back removing a leaf (from disk). pos:" << outRef.leafPos << "ShortHash:" << Log::Hex <<shortHash;

        // if the bucket exists, we add it. Otherwise we create a new bucket for this leaf.
        uint32_t bucketId = m_jumptables[shortHash];
        Bucket *bucket = nullptr;
        if (bucketId >= MEMBIT) { // add leaf
            auto iter = m_buckets.find(static_cast<int32_t>(bucketId & MEMMASK));
            assert(iter != m_buckets.end());
            bucket = &iter->second;
        } else { // bucket is not in memory
            DEBUGUTXO << " + reloading a bucket from disk for this";
            Bucket memBucket;
            memBucket.fillFromDisk(Streaming::ConstBuffer(m_buffer, m_buffer.get() + bucketId,
                                                          m_buffer.get() + m_file.size()),
                                 static_cast<int>(bucketId));
            m_jumptables[shortHash] = static_cast<uint32_t>(m_nextBucketIndex) + MEMBIT;
            auto iterator = m_buckets.insert(std::make_pair(m_nextBucketIndex++, std::move(memBucket))).first;
            bucket = &iterator->second;
        }
        bucket->unspentOutputs.push_back(outRef);
        bucket->saveAttempt = 0;
    }

#ifndef NDEBUG
    // make sure that the newly inserted leafs are reachable
    for (auto leaf : m_leafsBackup) { // reinsert deleted leafs
        const uint256 txid = leaf.prevTxId();
        const uint32_t shortHash = createShortHash(txid);
        assert(shortHash < 0x100000);
        assert (m_jumptables[shortHash]);
        uint32_t bucketId = m_jumptables[shortHash];
        assert (bucketId >= MEMBIT);
        assert (m_buckets.find(bucketId & MEMMASK) != m_buckets.end());
        auto iter = m_buckets.find(bucketId & MEMMASK);
        bool found = false;
        for (OutputRef rev : iter->second.unspentOutputs) {
            if (rev.leafPos > MEMBIT) {
                auto leafIter = m_leafs.find(rev.leafPos & MEMMASK);
                assert(leafIter != m_leafs.end());
                if (leafIter->second.outIndex() == leaf.outIndex() && leafIter->second.prevTxId() == txid) {
                    found = true;
                }
            }
        }
        assert (found);
    }
    // validate state of jumptable
    for (int i = 0; i < 0x100000; ++i) {
        uint32_t bucketId = m_jumptables[i];
        assert(bucketId < MEMBIT || static_cast<int>(bucketId & MEMMASK) < m_nextBucketIndex);
        if (bucketId >= MEMBIT) {
            assert (m_buckets.find(bucketId & MEMMASK) != m_buckets.end());
        }
    }
    for (auto iter = m_buckets.begin(); iter != m_buckets.end(); ++iter) {
        const int bucketId = iter->first;
        assert(bucketId >= 0);
        assert(static_cast<uint32_t>(bucketId) <= MEMBIT);
        Bucket *bucket = &iter->second;
        assert(!bucket->unspentOutputs.empty());
        const auto shortHash = createShortHash(bucket->unspentOutputs.begin()->cheapHash);
        assert(shortHash < 0x100000);
        assert(m_jumptables[shortHash] >= MEMBIT);
        assert(m_jumptables[shortHash] == static_cast<uint32_t>(bucketId) + MEMBIT);
    }
#endif

    commit();
}

void DataFile::addChange(const UODBPrivate *priv)
{
    if (!m_flushScheduled && !priv->memOnly && ++m_changeCount > SAVE_CHUNK_SIZE) {
        m_flushScheduled = true;
        priv->ioService.post(std::bind(&DataFile::flushSomeNodesToDisk, this, NormalSave));
    }
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
                logFatal(Log::UTXO) << "Failed to create datafile, removing non-file failed";
                throw std::runtime_error("Failed to replace non-file");
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
    return df;
}


/////////////////////////////////////////////////////////////////////////

DataFileCache::DataFileCache(const boost::filesystem::path &baseFilename)
    : m_baseFilename(baseFilename)
{
    for (int i = 1; i < 10; ++i) {
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
    // if number of m_validInfoFiles are more than 4
    // delete the one with the lowest / oldest 'lastBlockHeight'
    while (m_validInfoFiles.size() > 4) {
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

    // find the first not used number and use that for our new datafile.
    int newIndex = -1;
    for (int i = 1; i < 10; ++i) {
        bool unused = true;
        for (const auto &info : m_validInfoFiles) {
            if (info.index == i) {
                unused = false;
                break;
            }
        }
        if (unused) {
            newIndex = i;
            break;
        }
    }

    assert(newIndex > 0);
    assert(newIndex < 10);

    boost::filesystem::remove(filenameFor(newIndex));
    std::string outFile = filenameFor(newIndex).string();
    std::ofstream out(outFile, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open())
        throw std::runtime_error("Failed to open info file for writing");

    Streaming::MessageBuilder builder(Streaming::NoHeader, 256);
    builder.add(UODB::FirstBlockHeight, source->m_initialBlockHeight);
    builder.add(UODB::LastBlockHeight, source->m_lastBlockHeight);
    builder.add(UODB::LastBlockId, source->m_lastBlockHash);
    builder.add(UODB::PositionInFile, source->m_writeBuffer.offset());
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
    logInfo(Log::UTXO) << "Loading" << filenameFor(info.index).string();
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
            else if (parser.tag() == UODB::PositionInFile) {
                target->m_writeBuffer = Streaming::BufferPool(target->m_buffer, static_cast<int>(target->m_file.size()), true);
                target->m_writeBuffer.markUsed(parser.intData());
                target->m_writeBuffer.forget(parser.intData());
            }
            else
                break;
        }
        posOfJumptable = parser.consumed();
    }
    in.seekg(posOfJumptable);
    in.read(reinterpret_cast<char*>(target->m_jumptables), sizeof(target->m_jumptables));

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
