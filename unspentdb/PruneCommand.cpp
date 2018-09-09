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
#include "PruneCommand.h"

#include <utxo/UnspentOutputDatabase_p.h>
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

#include <QFileInfo>
#include <QDir>
#include <hash.h>
#include <fstream>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/filesystem.hpp>

static void nothing(const char *){}

PruneCommand::PruneCommand()
    : m_force(QStringList() << "force", "Force pruning")
{
}

QString PruneCommand::commandDescription() const
{
    return "Prune\nTakes the selected database file and prunes already spent outputs";
}

void PruneCommand::addArguments(QCommandLineParser &parser)
{
    parser.addOption(m_force);
}

bool PruneCommand::prune(const std::string &dbFile, const std::string &infoFilename)
{
    out << "Counting buckets...";
    out.flush();
    std::ifstream in(infoFilename, std::ios::binary | std::ios::in);
    if (!in.is_open()) {
        err << endl << "Failed to read from info file" << endl;
        return false;
    }

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
        if (result != checksum) {
            err << endl << "Checksum of info file failed, prune aborted." << endl;
            return false;
        }
    }

    boost::iostreams::mapped_file file;
    file.open(dbFile, std::ios_base::binary | std::ios_base::in);
    if (!file.is_open()) {
        err << endl << "Failed to open db file" << endl;
        return false;
    }
    std::shared_ptr<char> buffer = std::shared_ptr<char>(const_cast<char*>(file.const_data()), nothing);

    std::vector<Bucket> buckets;
    buckets.reserve(100000);
    // Find all buckets
    for (int i = 0; i < 0x100000; ++i) {
        if (jumptable[i] == 0)
            continue;
        if (jumptable[i] > 0x7FFFFFFF) {
            err << endl << "Error found. Info file jumps to pos > 2GB" << endl;
            continue;
        }
        int32_t bucketOffsetInFile = static_cast<int>(jumptable[i]);
        if (bucketOffsetInFile > file.size()) {
            err << endl << "Error found. Info file jumps to pos greater than db file" << endl;
            continue;
        }

        Bucket bucket;
        bucket.shorthash = i;
        Streaming::ConstBuffer buf(buffer, buffer.get() + bucketOffsetInFile, buffer.get() + file.size());
        Streaming::MessageParser parser(buf);
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == UODB::LeafPosRelToBucket) {
                int offset = parser.intData();
                if (offset >= bucketOffsetInFile)
                    err << endl << "Error found. Offset to bucket leads to negative file position." << endl;
                else
                    bucket.leafPositions.push_back(bucketOffsetInFile - offset);
            }
            else if (parser.tag() == UODB::LeafPosition) {
                bucket.leafPositions.push_back(parser.intData());
            } else if (parser.tag() == UODB::Separator) {
                break;
            }
        }
        if (!bucket.leafPositions.empty())
            buckets.push_back(bucket);
    }
    out << " found: " << buckets.size() << endl;

    memset(jumptable, 0, sizeof(jumptable));
    const std::string outFilename(dbFile + ".new");
    {
        boost::filesystem::remove(outFilename);
        boost::filesystem::ofstream outFle(outFilename);
        outFle.close();
        boost::filesystem::resize_file(outFilename, 2147483600); // ~2GB // TODO estimate new size better!
    }
    int outFileSize = 0;
    {
        boost::iostreams::mapped_file outFile;
        outFile.open(outFilename, std::ios_base::binary | std::ios_base::out);
        if (!outFile.is_open()) {
            err << "Failed to open replacement db file for writing" << endl;
            return false;
        }
        out << "Copying leafs and buckets.";
        out.flush();
        std::shared_ptr<char> outStream = std::shared_ptr<char>(const_cast<char*>(outFile.const_data()), nothing);
        Streaming::BufferPool outBuf = Streaming::BufferPool(outStream, static_cast<int>(outFile.size()), true);
        Streaming::MessageBuilder builder(outBuf);
        int index = 0;
        int stop = buckets.size() / 50;
        for (const Bucket &bucket : buckets) {
            if (bucket.leafPositions.size() > 2)
                continue;
            if ((++index % stop) == 0) {
                out << ".";
                out.flush();
            }
            // copy leafs
            uint32_t newPos = copyBucket(bucket, buffer, outBuf, builder);
            jumptable[bucket.shorthash] = newPos;
        }
        out << " ";
        out.flush();
        for (const Bucket &bucket : buckets) {
            if (bucket.leafPositions.size() <= 2)
                continue;
            if ((++index % stop) == 0) {
                out << ".";
                out.flush();
            }
            // copy leafs
            uint32_t newPos = copyBucket(bucket, buffer, outBuf, builder);
            jumptable[bucket.shorthash] = newPos;
        }
        outFileSize = outBuf.offset();
        outFile.close();
    }
    file.close();
    out << " " << outFileSize << " bytes" << endl << "Writing new info file" << endl;

    // write new info file
    boost::filesystem::remove(infoFilename + ".new");
    std::ofstream outInfo(infoFilename + ".new", std::ios::binary | std::ios::out | std::ios::trunc);
    if (!outInfo.is_open()) {
        err << "Failed to open new index file" << endl;
        return Flowee::CommandFailed;
    }

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

    return true;
}

Flowee::ReturnCodes PruneCommand::run()
{
    DatabaseFile infoFile;
    if (dbDataFile().filetype() == InfoFile) {
        if (!commandLineParser().isSet(m_force)) {
            err << "You selected a specific info file instead of a database\n"
                   "this risks you might not use the latest version.\n\n"
                   "Select db file instead of pass --force if you don't mind losing data" << endl;
            return Flowee::NeedForce;
        }
        infoFile = dbDataFile();
    }
    else if (dbDataFile().filetype() == Datadir) {
        err << "Whole datadir pruning is not yet possible" << endl;
        return Flowee::InvalidOptions;
    }
    else {
        // we need to find the info file with the highest blockheight;
        // the cache takes a filename like the database, but without extensions
        QString path = dbDataFile().filepath();
        if (path.endsWith(".db"))
            path = path.mid(0, path.length() - 3);
        DataFileCache cache(path.toStdString());
        int highest = 0, index = -1;
        for (DataFileCache::InfoFile info : cache.m_validInfoFiles) {
            if (info.lastBlockHeight > highest) {
                highest = info.lastBlockHeight;
                index = info.index;
            }
        }
        for (auto info : dbDataFile().infoFiles()) {
            if (info.index() == index) {
                infoFile = info;
                break;
            }
        }
    }
    QFileInfo info(infoFile.filepath());
    if (!info.exists()) {
        err << "Failed to find an appropriate info file" << endl;
        return Flowee::InvalidOptions;
    }
    QFileInfo dbInfo(infoFile.databaseFiles().first().filepath());
    out << "Operating on " << dbInfo.fileName() << " and snapshot file " << info.fileName() << endl;

    const std::string dbFilePath = dbInfo.absoluteFilePath().toStdString();
    const std::string infoFilePath = info.absoluteFilePath().toStdString();
    if (prune(dbFilePath, infoFilePath)) {
        out << "Finishing up" << endl;
        // remove all old info files (they can no longer work) and rename the db file
        // and the info file over the original ones
        DataFileCache cache(dbInfo.dir().filePath(dbInfo.baseName()).toStdString());
        for (int i = 0; i < 10; ++i)
            boost::filesystem::remove(cache.filenameFor(i));

        boost::filesystem::rename(dbFilePath + ".new", dbFilePath);
        boost::filesystem::rename(infoFilePath + ".new", infoFilePath);
        fflush(nullptr);
        out << "Done" << endl;
        return Flowee::Ok;
    }
    return Flowee::CommandFailed;
}

uint32_t PruneCommand::copyBucket(const Bucket &bucket, const std::shared_ptr<char> &inputBuf, Streaming::BufferPool &outBuf, Streaming::MessageBuilder &builder)
{
    std::vector<uint64_t> cheapHashes;
    std::vector<uint32_t> diskPositions;
    for (auto pos : bucket.leafPositions) {
        bool failed = false;
        // read from old
        Streaming::ConstBuffer buf(inputBuf, inputBuf.get() + pos, inputBuf.get() + pos + 100);
        Leaf bucket = readLeaf(buf, &failed);
        if (failed || bucket.blockHeight < 1 || bucket.offsetInBlock <= 0 || bucket.outIndex < 0) {
            err << "Error found. Failed to parse Leaf at " << pos << endl;
        } else {
            diskPositions.push_back(outBuf.offset());
            cheapHashes.push_back(bucket.txid.GetCheapHash());
            builder.add(UODB::TXID, bucket.txid);
            if (bucket.outIndex != 0)
                builder.add(UODB::OutIndex, bucket.outIndex);
            builder.add(UODB::BlockHeight, bucket.blockHeight);
            builder.add(UODB::OffsetInBlock, bucket.offsetInBlock);
            builder.add(UODB::Separator, true);
        }
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
