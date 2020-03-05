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
#include "InfoCommand.h"

#include <streaming/BufferPool.h>
#include <streaming/MessageParser.h>

// private header as we read the db tags directly
#include <utxo/UnspentOutputDatabase_p.h>

#include <QFile>

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
    for (auto dataFile : dbDataFiles()) {
        for (auto infoFile : dataFile.infoFiles()) {
            out << "Working on checkpoint file; " << infoFile.filepath() << endl;
            auto checkpoint = readInfoFile(infoFile.filepath());
            if (checkpoint.jumptableFilepos < 0)
                continue;
            out << "Is Tip           : " << (checkpoint.isTip ? "yes" : "no") << endl;
            out << "Last Block ID    : " << QString::fromStdString(checkpoint.lastBlockId.GetHex()) << endl;
            out << "First Blockheight: " << checkpoint.firstBlockHeight << endl;
            out << "Last Blockheight : " << checkpoint.lastBlockHeight << endl;
            out << "Jumptable Hash   : " << QString::fromStdString(checkpoint.jumptableHash.GetHex()) << endl;
            out << "Filesize         : " << checkpoint.positionInFile << endl;
            out << "Changes Since GC : ";
            if (checkpoint.changesSincePrune == -1)
                out << "unset";
            else
                out << checkpoint.changesSincePrune;
            out << "\nPruned-index size: ";
            if (checkpoint.initialBucketSize == -1)
                out << "unset";
            else
                out << checkpoint.initialBucketSize;
            out << "\nInvalid blocks   : ";
            if (checkpoint.invalidBlockHashes.size() == 0)
                out << "none" << endl;
            else
                out << checkpoint.invalidBlockHashes.size() << endl;
            for (auto b : checkpoint.invalidBlockHashes) {
                out << "              ID : " << QString::fromStdString(b.ToString()) << endl;
            }

            if (commandLineParser().isSet(m_printUsage)) {
                uint32_t jumptables[0x100000];
                if (readJumptables(infoFile.filepath(), checkpoint.jumptableFilepos, jumptables)) {
                    if (checkpoint.jumptableHash != calcChecksum(jumptables))
                        err << "CHECKSUM Failed" << endl;
                    else
                        printStats(jumptables, infoFile);
                }
            }
            out << endl;
        }
    }

    return Flowee::Ok;
}

void InfoCommand::addArguments(QCommandLineParser &parser)
{
    parser.addOption(m_printUsage);
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
