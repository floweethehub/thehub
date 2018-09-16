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

uint32_t copyBucket(const Bucket &bucket, const std::shared_ptr<char> &inputBuf, Streaming::BufferPool &outBuf, Streaming::MessageBuilder &builder)
{
    std::vector<uint64_t> cheapHashes;
    std::vector<uint32_t> diskPositions;
    for (auto ref : bucket.unspentOutputs) {
        const uint32_t pos = ref.leafPos;
        // read from old
        Streaming::ConstBuffer buf(inputBuf, inputBuf.get() + pos, inputBuf.get() + pos + 100);
        UnspentOutput leaf(buf);
        if (leaf.blockHeight() < 1 || leaf.offsetInBlock() <= 0 || leaf.outIndex() < 0) {
            logCritical() << "Error found while copying a bucket, the leaf at pos-in-file" << pos
                          << "didn't have the required minimum info";
            throw std::runtime_error("Error found, failed to parse leaf");
        }
        diskPositions.push_back(outBuf.offset());
        uint256 txid = leaf.prevTxId();
        cheapHashes.push_back(txid.GetCheapHash());
        builder.add(UODB::TXID, txid);
        if (leaf.outIndex() != 0)
            builder.add(UODB::OutIndex, leaf.outIndex());
        builder.add(UODB::BlockHeight, leaf.blockHeight());
        builder.add(UODB::OffsetInBlock, leaf.offsetInBlock());
        builder.add(UODB::Separator, true);
    }
    assert(diskPositions.size() == cheapHashes.size());
    if (!diskPositions.empty()) {
        outBuf.commit();
        const uint32_t posOfBucket = outBuf.offset();
        uint64_t prevCH = 0;
        for (size_t i = 0; i < diskPositions.size(); ++i) {
            if (prevCH != cheapHashes.at(i)) {
                builder.add(UODB::CheapHash, cheapHashes.at(i));
                prevCH = cheapHashes.at(i);
            }
            assert(posOfBucket > diskPositions.at(i));
            int offset = posOfBucket - diskPositions.at(i);
            assert(offset > 0);
            builder.add(UODB::LeafPosRelToBucket, static_cast<int>(posOfBucket - diskPositions.at(i)));
        }
        builder.add(UODB::Separator, true);
        outBuf.commit();
        return posOfBucket;
    }
    return 0;
}
}

Pruner::Pruner(const std::string &dbFile, const std::string &infoFile)
    : m_dbFile(dbFile),
      m_infoFile(infoFile)
{
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
        boost::iostreams::mapped_file outFile;
        outFile.open(outFilename, std::ios_base::binary | std::ios_base::out);
        if (!outFile.is_open())
            throw std::runtime_error("Failed to open replacement db file for writing");
        logInfo() << "Pruning is now copying leafs and buckets";
        std::shared_ptr<char> outStream = std::shared_ptr<char>(const_cast<char*>(outFile.const_data()), nothing);
        Streaming::BufferPool outBuf = Streaming::BufferPool(outStream, static_cast<int>(outFile.size()), true);
        Streaming::MessageBuilder builder(outBuf);
        for (const Bucket &bucket : buckets) {
            if (bucket.unspentOutputs.size() > 2)
                continue;
            // copy leafs
            uint32_t newPos = copyBucket(bucket, buffer, outBuf, builder);
            if (newPos != 0)
                jumptable[createShortHash(bucket.unspentOutputs.front().cheapHash)] = newPos;
        }
        for (const Bucket &bucket : buckets) {
            if (bucket.unspentOutputs.size() <= 2)
                continue;
            // copy leafs
            uint32_t newPos = copyBucket(bucket, buffer, outBuf, builder);
            if (newPos != 0)
                jumptable[createShortHash(bucket.unspentOutputs.front().cheapHash)] = newPos;
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
