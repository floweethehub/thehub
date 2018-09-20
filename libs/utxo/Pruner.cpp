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

#include "UnspentOutputDatabase_p.h"
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>
#include <utils/random.h>

#include <fstream>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <server/hash.h>

namespace {
static void nothing(const char *){}

struct Leaf {
    int blockHeight = -1;
    int offsetInBlock = -1;
    int outIndex = 0;
    uint256 txid;
};

struct LeafRef
{
    uint32_t diskPosition;
    uint256 txid;
    int output;

    static bool compare(const LeafRef &one, const LeafRef &two) {
        if (one.txid == two.txid)
            return one.output < two.output;
        return one.txid < two.txid;
    }
};

enum Type {
    OnlyLeafs,
    OnlYBucket,
    BucketAndLeafs
};

std::vector<LeafRef> readLeafRefs(const Bucket &bucket, const std::shared_ptr<char> &inputBuf, size_t bufSize)
{
    std::vector<LeafRef> leafRefs;
    leafRefs.reserve(bucket.unspentOutputs.size());
    for (auto ref : bucket.unspentOutputs) {
        const uint32_t pos = ref.leafPos;
        // read from old
        Streaming::ConstBuffer buf(inputBuf, inputBuf.get() + pos, inputBuf.get() + bufSize);
        UnspentOutput leaf(ref.cheapHash, buf);
        if (leaf.blockHeight() < 1 || leaf.offsetInBlock() <= 0 || leaf.outIndex() < 0) {
            logCritical() << "Error found while copying a bucket, the leaf at pos-in-file" << pos
                          << "didn't have the required minimum info";
            throw std::runtime_error("Error found, failed to parse leaf");
        }
        leafRefs.push_back({pos, leaf.prevTxId(), leaf.outIndex()});
    }
    return leafRefs;
}

void copyLeafs(const std::shared_ptr<char> &inputBuf, size_t bufSize, Streaming::BufferPool &outBuf, Streaming::MessageBuilder &builder, std::vector<LeafRef> &leafRefs)
{
    for (size_t i = 0; i < leafRefs.size(); ++i) { // write leafs
        LeafRef &ref = leafRefs[i];
        const uint32_t pos = ref.diskPosition;
        // read from old
        Streaming::ConstBuffer buf(inputBuf, inputBuf.get() + pos, inputBuf.get() + bufSize);
        UnspentOutput leaf(ref.txid.GetCheapHash(), buf);
        const uint256 txid = leaf.prevTxId();
        ref.diskPosition = outBuf.offset();
        if (leafRefs.size() > i || leafRefs.at(i + 1).txid != txid) {
            builder.add(UODB::BlockHeight, leaf.blockHeight());
            builder.add(UODB::OffsetInBlock, leaf.offsetInBlock());
            std::vector<char> shortTxId;
            shortTxId.resize(24);
            memcpy(shortTxId.data(), txid.begin() + 8, 24);
            builder.add(UODB::TXID, shortTxId);
        }
        if (leaf.outIndex() != 0)
            builder.add(UODB::OutIndex, leaf.outIndex());
        builder.add(UODB::Separator, true);
    }
}

uint32_t writeBucketData(Streaming::BufferPool &outBuf, Streaming::MessageBuilder &builder, const std::vector<LeafRef> &leafRefs)
{
    outBuf.commit();
    const uint32_t posOfBucket = outBuf.offset();
    uint64_t prevCH = 0;
    for (size_t i = 0; i < leafRefs.size(); ++i) {
        const uint64_t cheapHash = leafRefs.at(i).txid.GetCheapHash();
        if (prevCH != cheapHash) {
            builder.add(UODB::CheapHash, cheapHash);
            prevCH = cheapHash;
        }
        assert(posOfBucket > leafRefs.at(i).diskPosition);
        int offset = posOfBucket - leafRefs.at(i).diskPosition;
        assert(offset > 0);
        builder.add(UODB::LeafPosRelToBucket, static_cast<int>(posOfBucket - leafRefs.at(i).diskPosition));
    }
    builder.add(UODB::Separator, true);
    outBuf.commit();
    return posOfBucket;
}

// copies the entire bucket, keeping the leafs and the bucket data as close together as possible
uint32_t copyBucket(const Bucket &bucket, const std::shared_ptr<char> &inputBuf, size_t bufSize, Streaming::BufferPool &outBuf, Streaming::MessageBuilder &builder)
{
    std::vector<LeafRef> leafRefs = readLeafRefs(bucket, inputBuf, bufSize);
    if (leafRefs.empty())
        return 0;
    std::sort(leafRefs.begin(), leafRefs.end(), &LeafRef::compare);
    copyLeafs(inputBuf, bufSize, outBuf, builder, leafRefs);
    return writeBucketData(outBuf, builder, leafRefs);
}
}

Pruner::Pruner(const std::string &dbFile, const std::string &infoFile, DBType dbType)
    : m_dbFile(dbFile),
      m_infoFile(infoFile),
      m_dbType(dbType)
{
    assert(m_dbType == OlderDB || m_dbType == MostActiveDB);
    char buf[20];
    snprintf(buf, 20, ".new%d", GetRandInt(INT_MAX));
    m_tmpExtension = std::string(buf);
}

void Pruner::commit()
{
    boost::filesystem::rename(m_dbFile + m_tmpExtension, m_dbFile);
    boost::filesystem::rename(m_infoFile + m_tmpExtension, m_infoFile);
}

void Pruner::cleanup()
{
    boost::filesystem::remove(m_dbFile + m_tmpExtension);
    boost::filesystem::remove(m_infoFile + m_tmpExtension);
}

void Pruner::prune()
{
    logCritical() << "Pruning" << m_dbFile;
    logInfo() << "Starting pruning. Counting buckets...";
    std::ifstream in(m_infoFile, std::ios::binary | std::ios::in);
    if (!in.is_open())
        throw std::runtime_error("Failed to open info file");

    int initialBlockHeight = -1;
    int lastBlockHeight = 1;
    uint256 lastBlockHash;
    int posInFile = 0;

    int posOfJumptable = 0;
    uint256 checksum;
    {
        std::shared_ptr<char> buf(new char[256], std::default_delete<char[]>());
        in.read(buf.get(), 256);
        Streaming::MessageParser parser(Streaming::ConstBuffer(buf, buf.get(), buf.get() + 256));
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == UODB::LastBlockHeight)
                lastBlockHeight = parser.intData();
            else if (parser.tag() == UODB::FirstBlockHeight)
                initialBlockHeight = parser.intData();
            else if (parser.tag() == UODB::LastBlockId)
                lastBlockHash = parser.uint256Data();
            else if (parser.tag() == UODB::JumpTableHash)
                checksum = parser.uint256Data();
            else if (parser.tag() == UODB::PositionInFile)
                posInFile = parser.intData();
            else if (parser.tag() == UODB::Separator)
                break;
        }
        posOfJumptable = parser.consumed();
    }
    in.seekg(posOfJumptable);
    uint32_t jumptable[0x100000];
    in.read(reinterpret_cast<char*>(jumptable), sizeof(jumptable));

    {
        CHash256 ctx;
        ctx.Write(reinterpret_cast<const unsigned char*>(jumptable), sizeof(jumptable));
        uint256 result;
        ctx.Finalize(reinterpret_cast<unsigned char*>(&result));
        if (result != checksum)
            throw std::runtime_error("info file is mangled, checksum failed");
    }

    boost::iostreams::mapped_file file;
    file.open(m_dbFile, std::ios_base::binary | std::ios_base::in);
    if (!file.is_open())
        throw std::runtime_error("Failed to open db file");

    std::shared_ptr<char> buffer = std::shared_ptr<char>(const_cast<char*>(file.const_data()), nothing);
    std::vector<Bucket> buckets;
    buckets.reserve(100000);
    // Find all buckets
    for (int i = 0; i < 0x100000; ++i) {
        if (jumptable[i] == 0)
            continue;
        if (jumptable[i] > 0x7FFFFFFF)
            throw std::runtime_error("Info file jumps to pos > 2GB. Needs to be repaired first.");
        int32_t bucketOffsetInFile = static_cast<int>(jumptable[i]);
        if (bucketOffsetInFile > file.size())
            throw std::runtime_error("Info file links to pos greater than DB file.");


        Bucket bucket;
        bucket.fillFromDisk(Streaming::ConstBuffer(buffer, buffer.get() + bucketOffsetInFile, buffer.get() + file.size()),
                             static_cast<int>(bucketOffsetInFile));
        // bucket.shorthash = i;
        if (!bucket.unspentOutputs.empty())
            buckets.push_back(bucket);
    }
    logInfo() << "Pruner found" << buckets.size() << "buckets";

    memset(jumptable, 0, sizeof(jumptable));
    const std::string outFilename(m_dbFile + m_tmpExtension);
    {
        boost::filesystem::remove(outFilename);
        boost::filesystem::ofstream outFle(outFilename);
        outFle.close();
        // new file size is all leafs (55 bytes each)
        // then the max 30 bytes to link to it from a bucket, times the amount of leafs in a bucket.
        // since we can expect a bucket to be re-written that (=amount of leafs) amount of times.
        int newFileSize = 0;
        for (auto bucket : buckets) {
            newFileSize += static_cast<int>(bucket.unspentOutputs.size()) * (55 + 30 + /* add some for security */ 20);
        }
        boost::filesystem::resize_file(outFilename, newFileSize);
    }
    int outFileSize = 0;
    {
        /*
         * If the DB is older we sort the buckets assuming that an access to the bucket will
         * lead to an access to the leaf.
         * This, in practice, means we keep write all almost-empty buckets at the front where
         * we first write the leafs immediately followed by the bucket.
         * Then we follow with the rest, following the assumption that larger buckets have greater
         * chance of being written out again at the end.
         *
         * For more recent database files we optimize to keep all the buckets together and thus keep
         * that part of the file in memory as much as possible.
         * This is more valuable here as many of the searches that are not hits still need to read
         * the bucket before they move to the next DB.
         *
         * For those more recent DBs, we write all the leafs first and append the buckets at the end.
         */

        boost::iostreams::mapped_file outFile;
        outFile.open(outFilename, std::ios_base::binary | std::ios_base::out);
        if (!outFile.is_open())
            throw std::runtime_error("Failed to open replacement db file for writing");
        logInfo() << "Pruning is now copying leafs and buckets";

        std::shared_ptr<char> outStream = std::shared_ptr<char>(const_cast<char*>(outFile.const_data()), nothing);
        Streaming::BufferPool outBuf = Streaming::BufferPool(outStream, static_cast<int>(outFile.size()), true);
        Streaming::MessageBuilder builder(outBuf);

        if (m_dbType == MostActiveDB) {
            for (const Bucket &bucket : buckets) {
                if (bucket.unspentOutputs.size() > 2)
                    continue;
                // copy buckets
                uint32_t newPos = copyBucket(bucket, buffer, file.size(), outBuf, builder);
                jumptable[createShortHash(bucket.unspentOutputs.front().cheapHash)] = newPos;
            }
            for (const Bucket &bucket : buckets) {
                if (bucket.unspentOutputs.size() <= 2)
                    continue;
                uint32_t newPos = copyBucket(bucket, buffer, file.size(), outBuf, builder);
                jumptable[createShortHash(bucket.unspentOutputs.front().cheapHash)] = newPos;
            }
        } else {
            // leafs first, buckets at end of file
            assert(m_dbType == OlderDB);
            // first we only copy the leafs.
            for (size_t index = 0; index < buckets.size(); ++index) {
                Bucket &bucket = buckets[index];
                assert(!bucket.unspentOutputs.empty());
                std::vector<LeafRef> leafRefs = readLeafRefs(bucket, buffer, file.size());
                assert(leafRefs.size() == bucket.unspentOutputs.size()); // we either copy everything or nothing
                copyLeafs(buffer, file.size(), outBuf, builder, leafRefs);
                size_t i = 0;
                for (auto iter = bucket.unspentOutputs.begin(); iter != bucket.unspentOutputs.end(); ++iter, ++i) {
                    assert(iter->cheapHash == leafRefs[i].txid.GetCheapHash());
                    iter->leafPos = leafRefs.at(i).diskPosition;
                }
            }
            // next we write the bucket
            for (size_t index = 0; index < buckets.size(); ++index) {
                const Bucket &bucket = buckets[index];
                assert(!bucket.unspentOutputs.empty());
                int32_t newPos = bucket.saveToDisk(outBuf);
                assert(newPos >= 0);
                jumptable[createShortHash(bucket.unspentOutputs.front().cheapHash)] = newPos;
            }
        }
        outFileSize = outBuf.offset();
        outFile.close();
    }
    file.close();
    logInfo() << outFileSize << "bytes written.";

    // write new info file
    boost::filesystem::remove(m_infoFile + m_tmpExtension);
    std::ofstream outInfo(m_infoFile + m_tmpExtension, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!outInfo.is_open())
        throw std::runtime_error("Failed to open new index file");

    Streaming::MessageBuilder builder(Streaming::NoHeader, 256);
    builder.add(UODB::FirstBlockHeight, initialBlockHeight);
    builder.add(UODB::LastBlockHeight, lastBlockHeight);
    builder.add(UODB::LastBlockId, lastBlockHash);
    builder.add(UODB::PositionInFile, outFileSize);
    {
        CHash256 ctx;
        ctx.Write(reinterpret_cast<const unsigned char*>(jumptable), sizeof(jumptable));
        uint256 result;
        ctx.Finalize(reinterpret_cast<unsigned char*>(&result));
        builder.add(UODB::JumpTableHash, result);
    }
    builder.add(UODB::Separator, true);
    Streaming::ConstBuffer header = builder.buffer();
    outInfo.write(header.constData(), header.size());
    outInfo.write(reinterpret_cast<const char*>(jumptable), sizeof(jumptable));
    outInfo.flush();
    outInfo.close();
}
