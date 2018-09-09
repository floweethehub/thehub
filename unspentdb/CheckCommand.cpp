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
#include "CheckCommand.h"

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/filesystem.hpp>
#include <streaming/ConstBuffer.h>
#include <streaming/MessageParser.h>
// private header for createShortHas()
#include <utxo/UnspentOutputDatabase_p.h>

static void nothing(const char *){}

CheckCommand::CheckCommand()
{
}

QString CheckCommand::commandDescription() const
{
    return "Check\nValidate the internal structure of the database";
}

Flowee::ReturnCodes CheckCommand::run()
{
    foreach (auto df, dbDataFile().infoFiles()) {
        out << "Working on info file; " << df.filepath() << endl;
        const auto checkpoint = readInfoFile(df.filepath());
        if (checkpoint.jumptableFilepos < 0)
            continue;
        uint32_t jumptables[0x100000];
        if (!readJumptabls(df.filepath(), checkpoint.jumptableFilepos, jumptables))
            continue;
        if (checkpoint.jumptableHash != calcChecksum(jumptables)) {
            err << "CHECKSUM Failed" << endl;
            continue;
        }

        out << "Checking jumptable";
        out.flush();
        // check if jump table links to positions after our highest filepos
        for (int i = 0; i < 0x100000; ++i) {
            if (jumptables[i] >= checkpoint.positionInFile) {
                err << "shorthash: " << i << " points to disk pos " << (jumptables[i] - checkpoint.positionInFile)
                    << " bytes after checkpoint file-pos" << endl;
                jumptables[i] = 0;
            }
        }
        out << " ok" << endl;

        auto dbs = df.databaseFiles();
        if (dbs.isEmpty()) {
            err << "Don't know which database file to open" << endl;
            continue;
        }

        out << "Opening DB file";
        out.flush();
        // open db file
        boost::iostreams::mapped_file file;
        file.open(dbs.first().filepath().toStdString(), std::ios_base::binary | std::ios_base::in);
        if (!file.is_open()) {
            err << "Failed to open db file" << endl;
            continue;
        }
        std::shared_ptr<char> buffer = std::shared_ptr<char>(const_cast<char*>(file.const_data()), nothing);

        out << " ok\nChecking buckets..." << endl;
        // read buckets
        for (int shorthash = 0; shorthash < 0x100000; ++shorthash) {
            if (jumptables[shorthash] == 0)
                continue;
            int32_t bucketOffsetInFile = static_cast<int>(jumptables[shorthash]);
            Streaming::ConstBuffer buf(buffer, buffer.get() + bucketOffsetInFile, buffer.get() + file.size());
            std::vector<int> leafPositions = readBucket(buf, bucketOffsetInFile);
            for (auto leafPos : leafPositions) {
                if (leafPos > checkpoint.positionInFile) {
                    err << "Leaf after checkpoint pos" << endl;
                    continue;
                }
                Streaming::ConstBuffer leafBuf(buffer, buffer.get() + leafPos, buffer.get() + file.size());
                Leaf leaf = readLeaf(leafBuf);
                const uint64_t cheapHash = leaf.txid.GetCheapHash();
                const uint32_t leafShorthash = createShortHash(cheapHash);
                if (shorthash != leafShorthash)
                    err << "Leaf found under bucket with different shorthashes " << shorthash << " != " << leafShorthash << endl;

                if (leaf.blockHeight > checkpoint.lastBlockHeight)
                    err << "Leaf belongs to a block older than this checkpoint" << leaf.blockHeight << endl;
                else if (leaf.blockHeight < checkpoint.firstBlockHeight)
                    err << "Leaf belongs to a block before this db file" << leaf.blockHeight << endl;
            }

            for (size_t n = 0; n < leafPositions.size(); ++n) {
                const int leafPos = leafPositions[n];
                if (leafPos > checkpoint.positionInFile)  continue;
                Streaming::ConstBuffer leafBuf(buffer, buffer.get() + leafPos, buffer.get() + file.size());
                Leaf leaf = readLeaf(leafBuf);
                for (size_t m = n + 1; m < leafPositions.size(); ++m) {
                    const int leafPos2 = leafPositions[m];
                    Streaming::ConstBuffer leafBuf2(buffer, buffer.get() + leafPos2, buffer.get() + file.size());
                    Leaf leaf2 = readLeaf(leafBuf2);
                    if (leaf.outIndex == leaf2.outIndex && leaf.txid == leaf2.txid) {
                        err << "One utxo-entry is duplicated. " << QString::fromStdString(leaf.txid.GetHex())
                            << " | " << leaf.outIndex;
                    }
                }
            }
        }
    }
    out << "Check finished" << endl;

    return Flowee::Ok;
}
