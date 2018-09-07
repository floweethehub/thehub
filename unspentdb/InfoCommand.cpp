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
#include "InfoCommand.h"

#include <streaming/BufferPool.h>
#include <streaming/MessageParser.h>

// private header as we read the db tags directly
#include <utxo/UnspentOutputDatabase_p.h>

#include <QFile>
#include <hash.h>

InfoCommand::InfoCommand()
    : m_printUsage(QStringList() << "v" << "print-usage", "Print how many entries this file contains")
{
}

QString InfoCommand::commandDescription() const
{
    return "Info\nChecks and prints details about the unspend output DB";
}

Flowee::ReturnCodes InfoCommand::run()
{
    Streaming::BufferPool pool(500);
    foreach (auto df, dbDataFile().infoFiles()) {
        QFile file(df.filepath());
        if (!file.open(QIODevice::ReadOnly)) {
            err << "Can't open file " << df.filepath() << endl;
            continue;
        }
        out << "Info file; " << df.filepath() << endl;
        pool.reserve(500);
        qint64 read = file.read(pool.begin(), 500);
        Streaming::MessageParser parser(pool.commit(read));
        Streaming::ParsedType type = parser.next();
        bool done = false;
        uint256 checksum;
        while (!done && type == Streaming::FoundTag) {
            switch (static_cast<UODB::MessageTags>(parser.tag())) {
            case UODB::Separator:
                done = true;
                break;
            case UODB::LastBlockId:
                out << "Last Block ID    : " << QString::fromStdString(parser.uint256Data().GetHex()) << endl;
                break;
            case UODB::FirstBlockHeight:
                out << "First Blockheight: " << parser.longData() << endl;
                break;
            case UODB::LastBlockHeight:
                out << "Last Blockheight : " << parser.longData() << endl;
                break;
            case UODB::JumpTableHash:
                checksum = parser.uint256Data();
                out << "Jumptable hash   : " << QString::fromStdString(checksum.GetHex()) << endl;
                break;
            case UODB::PositionInFile:
                out << "Filesize         : " << parser.longData() << endl;
                break;

            case UODB::TXID:
            case UODB::OutIndex:
            case UODB::BlockHeight:
            case UODB::OffsetInBlock:
            case UODB::LeafPosition:
            case UODB::LeafPosRelToBucket:
            case UODB::CheapHash:
                err << "Unexpected non-info tag found in info file. " << parser.tag() << endl;
                break;
            default:
                err << "Unknown tag found in info file. " << parser.tag() << endl;
                break;
            }
            if (!done)
                type = parser.next();
        }
        out << endl;
        if (commandLineParser().isSet(m_printUsage)) {
            try {
                uint32_t jumptables[0x100000];
                readJumptabls(&file, parser.consumed(), jumptables);
                if (checksum != calcChecksum(jumptables))
                    out << "CHECKSUM Failed";
                else
                    printStats(jumptables, df);
            } catch (const std::exception &) {
            }
        }
    }


    return Flowee::Ok;
}

void InfoCommand::addArguments(QCommandLineParser &parser)
{
    parser.addOption(m_printUsage);
}

void InfoCommand::readJumptabls(QFile *infoFile, int startPos, uint32_t *tables)
{
    Q_ASSERT(infoFile);
    Q_ASSERT(infoFile->isOpen());
    infoFile->seek(startPos);

    auto bytesRead = infoFile->read(reinterpret_cast<char*>(tables), 0x400000);
    if (bytesRead <= 0) {
        err << "Jumptable not present or file could not be read";
        throw std::runtime_error("err");
    }
    if (bytesRead < 0x400000) {
        err << "Hashtable truncated, expected " << 0x400000 << " bytes, got " << bytesRead << endl;
        throw std::runtime_error("err");
    }
}

uint256 InfoCommand::calcChecksum(uint32_t *tables) const
{
    CHash256 ctx;
    ctx.Write(reinterpret_cast<const unsigned char*>(tables), 0x400000);
    uint256 checksum;
    ctx.Finalize(reinterpret_cast<unsigned char*>(&checksum));
    return checksum;
}

void InfoCommand::printStats(uint32_t *tables, const DatabaseFile &df)
{
    std::vector<uint32_t> revs, sizes;
    revs.reserve(0x100000);
    sizes.reserve(0x100000);
    for (int i = 0; i < 0x100000; ++i) {
        if (tables[i]) {
            revs.push_back(tables[i]);
        }
    }
    std::sort(revs.begin(), revs.end());

    auto dbFile = df.databaseFiles().first();
    QFile database(dbFile.filepath());
    if (!database.open(QIODevice::ReadOnly)) {
        err << "Can't open attacked database file" << endl;
        throw std::runtime_error("fail");
    }
    Streaming::BufferPool pool(100000);
    char *start = pool.begin();
    Streaming::ConstBuffer buf(pool.commit(100000));

    size_t leafs = 0;
    for (auto pos : revs) {
        database.seek(pos);
        database.read(start, 100000);
        Streaming::MessageParser parser(buf);
        bool done = false;
        int bucketSize = 0;
        while (!done && parser.next() == Streaming::FoundTag) {
            switch (parser.tag()) {
            case UODB::CheapHash:
                bucketSize++;
                break;
            case UODB::LeafPosition: break;
            case UODB::LeafPosRelToBucket: break;
            case UODB::Separator: done = true; break;
            default:
                err << "Got unparsable tag in bucket" << endl;
            }
        }
        sizes.push_back(bucketSize);
        leafs += bucketSize;
    }
    std::sort(sizes.begin(), sizes.end());
    out << "Buckets found: " << revs.size() << "/1048576 (" << (revs.size() * 100 / 0x100000) << "%)" << endl;
    out << "   leafs: " << leafs << endl;
    out << "   leafs per bucket. Average: " << leafs / revs.size() << " Median: " << sizes.at(sizes.size() / 2) << endl;
}
