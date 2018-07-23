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

static std::uint32_t createShortHash(const uint256 &hash)
{
    auto txid = hash.begin();
    return (txid[0] << 12) + (txid[1] << 4) + ((txid[2] & 0xF0) >> 4);
}

static std::uint32_t createShortHash(uint64_t cheapHash)
{
    std::uint32_t answer = (cheapHash & 0xFF) << 12;
    answer += (cheapHash & 0xFF00) >> 4;
    answer += (cheapHash & 0xF00000) >> 20;
    return answer;
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
    while (!(indexMatched && txidMatched) && parser.next() == Streaming::FoundTag) {
        if (!txidMatched && parser.tag() == UODB::TXID) {
            if (txid == parser.uint256Data())
                txidMatched = true;
            else
                return false;
        }
        else if (!indexMatched && parser.tag() == UODB::OutIndex) {
            if (index == parser.intData())
                indexMatched = true;
            else
                return false;
        }
        else if (!indexMatched && parser.tag() == UODB::BlockHeight)
            // if there is no OutIndex in the stream, it was zero
            indexMatched = 0 == index;
        else if (!indexMatched && parser.tag() == UODB::Separator)
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
    pool.reserve(80);
    Streaming::MessageBuilder builder(pool);
    builder.add(UODB::TXID, txid);
    if (outIndex != 0)
        builder.add(UODB::OutIndex, outIndex);
    builder.add(UODB::BlockHeight, blockHeight);
    builder.add(UODB::OffsetInBlock, offsetInBlock);
    builder.add(UODB::Separator, true);
    m_data = pool.commit();
}

UnspentOutput::UnspentOutput(const Streaming::ConstBuffer &buffer)
    : m_data(buffer),
      m_outIndex(0),
      m_offsetInBlock(-1),
      m_blockHeight(-1)
{
    Streaming::MessageParser parser(m_data);
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == UODB::BlockHeight)
            m_blockHeight = parser.intData();
        else if (parser.tag() == UODB::OffsetInBlock)
            m_offsetInBlock = parser.intData();
        else if (parser.tag() == UODB::OutIndex)
            m_outIndex = parser.intData();
        else if (parser.tag() == UODB::Separator)
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
        if (parser.tag() == UODB::TXID)
            return parser.uint256Data();
        else if (parser.tag() == UODB::Separator)
            break;
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
    flush();
    delete d;
}

UnspentOutputDatabase *UnspentOutputDatabase::createMemOnlyDB(const boost::filesystem::path &basedir)
{
    boost::asio::io_service ioService;
    auto d = new UODBPrivate(ioService, basedir);
    d->memOnly = true;
    return new UnspentOutputDatabase(d);
}

void UnspentOutputDatabase::insert(const uint256 &txid, int outIndex, int blockHeight, int offsetInBlock)
{
    DataFile *df;
    {
        std::lock_guard<std::mutex> lock(d->lock);
        df = d->dataFiles.back();
        if (df->m_fileFull) {
            DEBUGUTXO << "Creating a new DataFile" << d->dataFiles.size();
            d->dataFiles.push_back(DataFile::createDatafile(d->filepathForIndex(static_cast<int>(d->dataFiles.size() + 1)),
                    df->m_lastBlockheight, df->m_lastBlockHash));
            df = d->dataFiles.back();
        }
    }

    df->insert(d, txid, outIndex, blockHeight, offsetInBlock);
}

UnspentOutput UnspentOutputDatabase::find(const uint256 &txid, int index) const
{
    std::vector<DataFile*> dataFiles;
    {
        std::lock_guard<std::mutex> lock(d->lock);
        dataFiles = d->dataFiles;
    }

    for (size_t i = dataFiles.size(); i > 0; --i) {
        auto answer = dataFiles[i - 1]->find(txid, index);
        if (answer.isValid()) {
            answer.m_datafile = static_cast<int>(i);
            return answer;
        }
    }
    return UnspentOutput();
}

SpentOutput UnspentOutputDatabase::remove(const uint256 &txid, int index, int dbHint)
{
    SpentOutput done;
    if (dbHint == -1) { // we don't know which one holds the data, which means we'll have to try all until we got a hit.
        std::vector<DataFile*> dataFiles;
        {
            std::lock_guard<std::mutex> lock(d->lock);
            dataFiles = d->dataFiles;
        }
        for (size_t i = dataFiles.size(); i > 0; --i) {
            done  = dataFiles[i - 1]->remove(d, txid, index);
            if (done.isValid())
                break;
        }
    }
    else {
        assert(dbHint > 0);
        DataFile *df;
        {
            std::vector<DataFile*> dataFiles;
            if (dbHint >= static_cast<int>(d->dataFiles.size()))
                throw std::runtime_error("dbHint out of range");
            df = d->dataFiles.at(static_cast<size_t>(dbHint));
        }
        done  = df->remove(d, txid, index);
    }
    return done;
}

void UnspentOutputDatabase::blockFinished(int blockheight, const uint256 &blockId)
{
    std::lock_guard<std::mutex> lock(d->lock);
    auto df = d->dataFiles.back();
    std::lock_guard<std::recursive_mutex> lock2(df->m_lock);
    df->m_lastBlockHash = blockId;
    df->m_lastBlockheight = blockheight;

    int totalChanges = 0;
    for (auto df : d->dataFiles) {
        std::lock_guard<std::recursive_mutex> lock3(df->m_lock);
        totalChanges += df->m_changesSinceJumptableWritten;
    }

    if (totalChanges > 10000000) { // every 10 million inserts/deletes, auto-flush jumptables
        for (auto df : d->dataFiles) {
            std::lock_guard<std::recursive_mutex> lock3(df->m_lock);
            df->flushAll();
            df->m_changesSinceJumptableWritten = 0;
        }
    }
}

int UnspentOutputDatabase::blockheight() const
{
    std::lock_guard<std::mutex> lock(d->lock);
    return d->dataFiles.back()->m_lastBlockheight;
}

uint256 UnspentOutputDatabase::blockId() const
{
    std::lock_guard<std::mutex> lock(d->lock);
    return d->dataFiles.back()->m_lastBlockHash;
}

void UnspentOutputDatabase::flush()
{
    if (d->memOnly)
        return;
    std::lock_guard<std::mutex> lock(d->lock);
    for (auto df : d->dataFiles) {
        df->flushAll();
    }
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
    if (dataFiles.empty())
        dataFiles.push_back(DataFile::createDatafile(filepathForIndex(1), 0, uint256()));
}


UODBPrivate::~UODBPrivate()
{
    for (auto df : dataFiles) {
        delete df;
    }
}

void UODBPrivate::flushNodesToDisk()
{
    assert(!memOnly);
    std::vector<DataFile*> dataFilesCopy;
    {
        std::lock_guard<std::mutex> mutex(lock);
        dataFilesCopy = dataFiles;
    }

    for (auto df : dataFilesCopy) {
        try {
            df->flushSomeNodesToDisk(NormalSave);
        } catch(const std::exception &) {
            logFatal(Log::UTXO) << "Internal error; Failed to flush some nodes to disk.";
            // This method is likely called in a worker thread, throwing has no benefit.
        }
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
    unspentOutputs.clear();
    Streaming::MessageParser parser(buffer);
    uint64_t cheaphash = 0;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == UODB::CheapHash) {
            cheaphash = parser.longData();
        }
        else if (parser.tag() == UODB::LeafPosRelToBucket) {
            int offset = parser.intData();
            if (offset >= bucketOffsetInFile)
                throw std::runtime_error("Database corruption, offset to bucket messed up");
            unspentOutputs.push_back( {cheaphash,
                                       static_cast<std::uint32_t>(bucketOffsetInFile - offset)} );
        }
        else if (parser.tag() == UODB::LeafPosition) {
            unspentOutputs.push_back( {cheaphash, static_cast<std::uint32_t>(parser.intData())} );
        }
        else if (parser.tag() == UODB::Separator) {
            return;
        }
    }
    throw std::runtime_error("Failed to parse bucket");
}

int32_t Bucket::saveToDisk(Streaming::BufferPool &pool)
{
    const int32_t offset = pool.offset();

    Streaming::MessageBuilder builder(pool);
    uint64_t prevCH = 0;
    for (auto item : unspentOutputs) {
        if (prevCH != item.cheapHash) {
            builder.add(UODB::CheapHash, item.cheapHash);
            prevCH = item.cheapHash;
        }

        /*
         * Maybe use LeafPositionRelativeBucket tag
         * to have smaller numbers and we save the one that occupies the lowest amount of bytes
         */
        assert(offset >= 0);
        assert((item.leafPos & MEMBIT) == 0);
        assert(item.leafPos < static_cast<std::uint32_t>(offset));
        int byteCount = Streaming::serialisedIntSize(static_cast<int>(item.leafPos));
        const int offsetFromBucketSize = Streaming::serialisedIntSize(offset - static_cast<int>(item.leafPos));
        UODB::MessageTags tagToUse = UODB::LeafPosition;
        if (offsetFromBucketSize < byteCount)
            tagToUse = UODB::LeafPosRelToBucket;

        if (tagToUse == UODB::LeafPosRelToBucket)
            builder.add(tagToUse, offset - static_cast<int>(item.leafPos));
        else
            builder.add(tagToUse, static_cast<int>(item.leafPos));
    }
    builder.add(UODB::Separator, true);
    pool.commit();
    return offset;
}


//////////////////////////////////////////////////////////////////////////////////////////

static void nothing(const char *){}

DataFile::DataFile(const boost::filesystem::path &filename)
    :  m_memBuffers(100000),
      m_path(filename)
{
    memset(m_jumptables, 0, sizeof(m_jumptables));

    DataFileCache cache(m_path);
    auto dbFile(filename);
    dbFile.concat(".db");
    m_file.open(dbFile, std::ios_base::binary | std::ios_base::in | std::ios_base::out);
    if (!m_file.is_open())
        throw std::runtime_error("Failed to open file read/write");
    m_buffer = std::shared_ptr<char>(const_cast<char*>(m_file.const_data()), nothing);
    m_writeBuffer = Streaming::BufferPool(m_buffer, static_cast<int>(m_file.size()), true);

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
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    const std::int32_t leafPos = m_leafIndex++;
    m_leafs.insert(std::make_pair(leafPos, UnspentOutput(m_memBuffers, txid, outIndex, blockHeight, offsetInBlock)));
    const uint32_t shortHash = createShortHash(txid);
    DEBUGUTXO << "Insert leaf" << Log::Hex << shortHash << (leafPos & MEMMASK);
    Bucket *bucket;
    uint32_t bucketId = m_jumptables[shortHash];
    if (bucketId == 0) {
        bucketId = static_cast<uint32_t>(m_bucketIndex++);
        DEBUGUTXO << Log::Hex << "  + new. BucketId:" << bucketId;
        auto iterator = m_buckets.insert(std::make_pair(bucketId, Bucket())).first;
        bucket = &iterator->second;
        m_jumptables[shortHash] = bucketId + MEMBIT;
    } else if (bucketId & MEMBIT) { // highest bit is set. Bucket is in memory.
        bucket = &m_buckets.at(bucketId & MEMMASK);
    } else {
        // copy from disk and insert into memory so it can be saved later
        auto iterator = m_buckets.insert(std::make_pair(m_bucketIndex, Bucket())).first;
        bucket = &iterator->second;
        m_jumptables[shortHash] = static_cast<uint32_t>(m_bucketIndex) + MEMBIT;

        if (bucketId >= m_file.size()) // data corruption
            throw std::runtime_error("Bucket points past end of file.");
        bucket->fillFromDisk(Streaming::ConstBuffer(m_buffer, m_buffer.get() + bucketId, m_buffer.get() + m_file.size()),
                             static_cast<int>(bucketId));

        DEBUGUTXO << Log::Hex << "  + from disk, bucketId now:" << m_bucketIndex;
        m_bucketIndex++;
    }

    bucket->unspentOutputs.push_back({txid.GetCheapHash(), static_cast<std::uint32_t>(leafPos) + MEMBIT});
    bucket->saveAttempt = 0;

    if (!m_flushScheduled && !priv->memOnly && ++m_changeCount > SAVE_CHUNK_SIZE) {
        m_flushScheduled = true;
        priv->ioService.post(std::bind(&DataFile::flushSomeNodesToDisk, this, NormalSave));
    }
}

UnspentOutput DataFile::find(const uint256 &txid, int index) const
{
    const uint32_t shortHash = createShortHash(txid);
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    uint32_t bucketId = m_jumptables[shortHash];
    DEBUGUTXO << txid << index << Log::Hex << shortHash;;
    if (bucketId == 0) // not found
        return UnspentOutput();

    const Bucket *bucket;
    Bucket memBucket;
    if (bucketId & MEMBIT) { // highest bit is set. Bucket is in memory.
        try {
            bucket = &m_buckets.at(bucketId & MEMMASK);
            assert(bucket);
        } catch (const std::exception &e) {
            logDebug(Log::UTXO) << e;
            logDebug(Log::UTXO) << "Bucket inconsistency" << Log::Hex << bucketId;
            assert(false);
            throw;
        }
    } else {
        // copy from disk
        if (bucketId >= m_file.size()) // data corruption
            throw std::runtime_error("Bucket points past end of file.");

        memBucket.fillFromDisk(Streaming::ConstBuffer(m_buffer, m_buffer.get() + bucketId, m_buffer.get() + m_file.size()),
                               static_cast<std::int32_t>(bucketId));
        bucket = &memBucket;
    }

    // find all the possible hits. This is based on a 64 bits cheap-hash, so we can expects
    // near-hits on txid and we expect all outputs of those tx's to match.
    const auto cheapHash = txid.GetCheapHash();
    std::vector<std::uint32_t> offsets;
    for (auto uo : bucket->unspentOutputs) {
        if (uo.cheapHash ==  cheapHash) {
            offsets.push_back(uo.leafPos);
        }
    }
    // find out which one actually matches.
    for (auto leafOffset : offsets) {
        if (leafOffset & MEMBIT) {
            try {
                const UnspentOutput *output = &m_leafs.at(leafOffset & MEMMASK);
                if (matchesOutput(output->data(), txid, index))
                    return *output;
            } catch (const std::exception &e) {
                logDebug(Log::UTXO) << e;
                logDebug(Log::UTXO) << "Leaf not found (db inconsistency)" << Log::Hex << leafOffset;
                assert(false);
                throw;
            }
        } else { // its on disk, open through our memmap
            if (leafOffset >= m_file.size()) // data corruption
                throw std::runtime_error("Leaf points past end of file.");

            Streaming::ConstBuffer buf(m_buffer, m_buffer.get() + leafOffset, m_buffer.get() + m_file.size());
            if (matchesOutput(buf, txid, index))
                return UnspentOutput(buf);
        }
    }

    return UnspentOutput();
}

SpentOutput DataFile::remove(const UODBPrivate *priv, const uint256 &txid, int index)
{
    SpentOutput answer;
    const uint32_t shortHash = createShortHash(txid);
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    uint32_t bucketId = m_jumptables[shortHash];
    if (bucketId == 0) // not found
        return answer;

    Bucket *bucket;
    Bucket memBucket;
    if (bucketId & MEMBIT) { // highest bit is set. Bucket is in memory.
        bucket = &m_buckets.at(bucketId & MEMMASK);
        assert(bucket);
    } else {
        // copy from disk
        if (bucketId >= m_file.size()) // data corruption
            throw std::runtime_error("Bucket points past end of file.");

        memBucket.fillFromDisk(Streaming::ConstBuffer(m_buffer, m_buffer.get() + bucketId, m_buffer.get() + m_file.size()),
                               static_cast<std::int32_t>(bucketId));
        bucket = &memBucket;
    }

    const auto cheapHash = txid.GetCheapHash();
    for (auto iter = bucket->unspentOutputs.begin(); iter != bucket->unspentOutputs.end(); ++iter) {
        if (iter->cheapHash ==  cheapHash) {
            if (iter->leafPos & MEMBIT) {
                const auto leafIter = m_leafs.find(iter->leafPos & MEMMASK);
                assert(leafIter != m_leafs.end());
                UnspentOutput *output = &(leafIter->second);
                if (!matchesOutput(output->data(), txid, index))
                    continue;
                // found!
                answer.blockHeight = leafIter->second.blockHeight();
                answer.offsetInBlock = leafIter->second.offsetInBlock();
                m_leafs.erase(leafIter);
            } else { // its on disk
                if (iter->leafPos >= m_file.size()) // data corruption
                    throw std::runtime_error("Leaf points past end of file.");
                Streaming::ConstBuffer buf(m_buffer, m_buffer.get() + iter->leafPos, m_buffer.get() + m_file.size());
                if (!matchesOutput(buf, txid, index))
                    continue;
                // found!
                UnspentOutput uo(buf);
                answer.blockHeight = uo.blockHeight();
                answer.offsetInBlock = uo.offsetInBlock();
            }

            // found it. Now update the bucket to no longer refer to it.
            bucket->unspentOutputs.erase(iter);
            bucket->saveAttempt = 0;
            if (bucketId & MEMBIT) { // highest bit is set. Bucket is in memory.
                if (bucket->unspentOutputs.empty()) { // remove if empty
                    m_buckets.erase(m_buckets.find(bucketId & MEMMASK));
                    m_jumptables[shortHash] = 0;
                }
            } else {
                // on disk, then we need to copy it into the buckets map. But only if its not empty.
                if (bucket->unspentOutputs.empty()) {
                    // remove from jumptable
                    m_jumptables[shortHash] = 0;
                } else {
                    m_buckets.insert(std::make_pair(m_bucketIndex, memBucket));
                    m_jumptables[shortHash] = static_cast<std::uint32_t>(m_bucketIndex++) + MEMBIT;
                }
            }
            break;
        }
    }

    if (!m_flushScheduled && !priv->memOnly && ++m_changeCount > SAVE_CHUNK_SIZE) {
        m_flushScheduled = true;
        priv->ioService.post(std::bind(&DataFile::flushSomeNodesToDisk, this, NormalSave));
    }

    return answer;
}

void DataFile::flushSomeNodesToDisk(ForceBool force)
{
    logInfo(Log::UTXO) << "Flush nodes starting" << m_path.filename().string();
    logInfo(Log::UTXO) << " += Leafs in mem:" << m_leafs.size() << "buckets in mem:" << m_buckets.size();
    std::list<OutputRef> unsavedOutputs;
    std::unordered_map<int, UnspentOutput> leafs;
    std::set<uint32_t> bucketsToSave;
    // first gather the stuff we want to save, we need the mutex as this is stored in various std::lists
    {
        std::lock_guard<std::recursive_mutex> lock(m_lock);
        leafs = m_leafs;

        // Collect buckets (at least their content) we are going to store to disk.
        for (auto iter = m_buckets.begin(); iter != m_buckets.end(); ++iter) {
            assert(!iter->second.unspentOutputs.empty());
            const short saveAttempt = ++iter->second.saveAttempt;
            // we only save the bucket when the amount of outputs is large or after a while,
            // based on saveCount
            const bool forceSave = force == ForceSave || saveAttempt > 3 || iter->second.unspentOutputs.size() > 10;
            if (forceSave) {
                bucketsToSave.insert(createShortHash(iter->second.unspentOutputs.begin()->cheapHash));
                unsavedOutputs.insert(unsavedOutputs.end(), iter->second.unspentOutputs.begin(), iter->second.unspentOutputs.end());
            } else {
                // if we won't save the bucket, then only copy the leafs that need saving.
                for (auto leaf : iter->second.unspentOutputs) {
                    if (leaf.leafPos & MEMBIT) // is in-mem
                        unsavedOutputs.insert(unsavedOutputs.end(), leaf);
                }
            }
            if (unsavedOutputs.size() > SAVE_CHUNK_SIZE * 5)
                break;
        }
    }
    if (unsavedOutputs.empty())
        return;

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
                if (leaf != leafs.end()) {
                    const std::uint32_t offset = static_cast<std::uint32_t>(saveLeaf(leaf->second));
                    leafsFlushedToDisk++;
                    assert((offset & MEMBIT) == 0);
                    leafOffsets.insert(std::make_pair(begin->leafPos, offset)); // remember new offset to update real bucket
                    updatedBucket.unspentOutputs.back().leafPos = offset;
                }
            }
            assert((updatedBucket.unspentOutputs.back().leafPos & MEMBIT) == 0);
            ++begin;
        }
        flushedToDiskCount += leafsFlushedToDisk;

        if (bucketsToSave.find(shortHash) != bucketsToSave.end()) {
            flushedToDiskCount++;
            auto offset = updatedBucket.saveToDisk(m_writeBuffer);
            bucketOffsets.insert(std::make_pair(shortHash, offset));
        }

        end = nextBucket(unsavedOutputs, begin);
    }
    while (end != unsavedOutputs.end());

    std::lock_guard<std::recursive_mutex> lock(m_lock);
    DEBUGUTXO << " +~ Leafs in mem:" << m_leafs.size() << "buckets in mem:" << m_buckets.size();
    begin = unsavedOutputs.begin();
    end = nextBucket(unsavedOutputs, begin);
    do {
        int bucketSize = 0;
        const auto shortHash = createShortHash(begin->cheapHash);
        assert(shortHash < 0x100000);
        const uint32_t bucketId = m_jumptables[shortHash];
        if (bucketId == 0) {
            // then the bucket and all its contents has been removed already in parallel to our saving it
            begin = end;
            end = nextBucket(unsavedOutputs, begin);
            continue; // next bucket
        }
        auto bucketIter = m_buckets.find(bucketId & MEMMASK);
        assert(bucketIter != m_buckets.end());
        Bucket *bucket = &bucketIter->second;
        assert(bucket);
        assert(!bucket->unspentOutputs.empty()); // the remove code should have removed empty buckets.

        // for each leaf. Remove from memory and update pointers to saved version.
        while (begin != end) {
            auto newOffset = leafOffsets.find(begin->leafPos);
            if (newOffset != leafOffsets.end()) {
                auto oldLeaf = m_leafs.find(begin->leafPos & MEMMASK);
                if (oldLeaf != m_leafs.end()) {
                    // DEBUGUTXO << "remove leaf" << Log::Hex << begin->leafPos;
                    m_leafs.erase(oldLeaf);
                    for (auto iter = bucket->unspentOutputs.begin(); iter != bucket->unspentOutputs.end(); ++iter) {
                        if (iter->leafPos == begin->leafPos) {
                            // DEBUGUTXO << Log::Hex << " + === changing to" << newOffset->second << " for bucket" << bucketId;
                            iter->leafPos = newOffset->second;
                            break;
                        }
                    }
                }
            }

            ++begin;
            ++bucketSize;
        }

        const auto savedOffset = bucketOffsets.find(shortHash); // only present if we decided to save it
        // Now check if we can remove the bucket from memory
        if (savedOffset != bucketOffsets.end()
                && static_cast<int>(bucket->unspentOutputs.size()) == bucketSize) { // nothing added/removed since we saved it.
            bool allSaved = true;
            for (auto i : bucket->unspentOutputs) {
                if ((i.leafPos & MEMBIT) > 0) {
                    allSaved = false;
                    break;
                }
            }
            if (allSaved) {
                // replace pointers to use the saved bucket.
                m_jumptables[shortHash] = bucketOffsets.at(shortHash);
                m_buckets.erase(bucketIter);
            }
        }

        end = nextBucket(unsavedOutputs, begin);
    }
    while (end != unsavedOutputs.end());

    logInfo(Log::UTXO) << "Flushed" << flushedToDiskCount << "to disk. Filesize now:" << m_writeBuffer.offset();
    logInfo(Log::UTXO) << " +- Leafs in mem:" << m_leafs.size() << "Buckets in mem:" << m_buckets.size();
    m_flushScheduled = false;
    m_changeCount = 0;

    m_jumptableNeedsSave = true;
    if (!m_fileFull && m_writeBuffer.offset() > 1100000000) // 1.1GB
        m_fileFull = true;
    m_changesSinceJumptableWritten += flushedToDiskCount;
}

void DataFile::flushAll()
{
    DEBUGUTXO;
    // wait for saving in other thread to finish.
    while (!m_buckets.empty()) { // save all, the UnspentOutputDatabase has a lock that makes this a stop-the-world event
        flushSomeNodesToDisk(ForceSave);
    }
    m_bucketIndex = 0;
    m_buckets.clear();
    m_leafIndex = 0;
    m_leafs.clear();
    m_memBuffers.clear();
    if (!m_jumptableNeedsSave)
        return;

    DataFileCache cache(m_path);
    cache.writeInfoFile(this);
    m_jumptableNeedsSave = false;
}

int32_t DataFile::saveLeaf(const UnspentOutput &uo)
{
    const int32_t offset = m_writeBuffer.offset();
    assert(uo.data().size() > 0);
    memcpy(m_writeBuffer.begin(), uo.data().begin(), static_cast<size_t>(uo.data().size()));
    m_writeBuffer.commit(uo.data().size());
    return offset;
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
        boost::filesystem::resize_file(dbFile, 2147483600); // ~2GB
    }

    DataFile *df = new DataFile(filename);
    df->m_initialBlockHeight = firstBlockHeight;
    df->m_lastBlockheight = firstBlockHeight;
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

void DataFileCache::writeInfoFile(DataFile *source)
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

    std::ofstream out(filenameFor(newIndex).string(), std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open())
        throw std::runtime_error("Failed to open info file for writing");

    Streaming::MessageBuilder builder(Streaming::NoHeader, 256);
    builder.add(UODB::FirstBlockHeight, source->m_initialBlockHeight);
    builder.add(UODB::LastBlockHeight, source->m_lastBlockheight);
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
}

bool DataFileCache::load(const DataFileCache::InfoFile &info, DataFile *target)
{
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
                target->m_lastBlockheight = parser.intData();
            else if (parser.tag() == UODB::FirstBlockHeight)
                target->m_initialBlockHeight = parser.intData();
            else if (parser.tag() == UODB::LastBlockId)
                target->m_lastBlockHash = parser.uint256Data();
            else if (parser.tag() == UODB::JumpTableHash)
                checksum = parser.uint256Data();
            else if (parser.tag() == UODB::PositionInFile) {
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
