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
#include "CheckCommand.h"

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/filesystem.hpp>
#include <streaming/ConstBuffer.h>
#include <streaming/MessageParser.h>
// private header for createShortHash()
#include <utxo/UnspentOutputDatabase_p.h>

static void nothing(const char *){}

namespace {
void updateOutput(QTextStream &out, int current, int max)
{
    const int progressBefore = ((current - 1) * 50) / max;
    const int progressAfter = (current * 50) / max;
    for (int i = progressBefore; i < progressAfter; ++i) {
        out << ".";
        if ((progressAfter % 10) == 0)
            out << (progressAfter * 2) << "%";
        out.flush();
    }
}

}

CheckCommand::CheckCommand()
{
}

QString CheckCommand::commandDescription() const
{
    return "Check\nValidate the internal structure of the database";
}

Flowee::ReturnCodes CheckCommand::run()
{
    for (auto dataFile : dbDataFiles()) {
        for (auto infoFile : dataFile.infoFiles()) {
            out << "Working on info file; " << infoFile.filepath() << endl;
            const auto checkpoint = readInfoFile(infoFile.filepath());
            if (checkpoint.jumptableFilepos < 0)
                continue;
            uint32_t jumptables[0x100000];
            if (!readJumptables(infoFile.filepath(), checkpoint.jumptableFilepos, jumptables))
                continue;
            if (checkpoint.jumptableHash != calcChecksum(jumptables)) {
                err << "CHECKSUM Failed" << endl;
                continue;
            }

            out << "Checking jumptable";
            out.flush();
            // check if jump table links to positions after our highest filepos
            for (int i = 0; i < 0x100000; ++i) {
                if (jumptables[i] > 0 && jumptables[i] >= checkpoint.positionInFile) {
                    err << "shorthash: " << i << " points to disk pos " << (jumptables[i] - checkpoint.positionInFile)
                        << " bytes after checkpoint file-pos" << endl;
                    jumptables[i] = 0;
                }
            }
            out << " ok" << endl;

            auto dbs = infoFile.databaseFiles();
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

            int bucketCount = 0;
            for (int shorthash = 0; shorthash < 0x100000; ++shorthash) {
                if (jumptables[shorthash] != 0)
                    ++bucketCount;
            }

            out << " ok\nChecking buckets: ";
            out.flush();
            // read buckets
            int bucketsChecked = 0;
            for (int shorthash = 0; shorthash < 0x100000; ++shorthash) {
                if (jumptables[shorthash] == 0)
                    continue;
                updateOutput(out, ++bucketsChecked, bucketCount);
                int32_t bucketOffsetInFile = static_cast<int>(jumptables[shorthash]);
                Streaming::ConstBuffer buf(buffer, buffer.get() + bucketOffsetInFile, buffer.get() + file.size());
                std::vector<LeafRef> leafRefs = readBucket(buf, bucketOffsetInFile);
                for (auto leafRef : leafRefs) {
                    if (leafRef.pos > checkpoint.positionInFile) {
                        err << "Leaf after checkpoint pos" << endl;
                        continue;
                    }
                    Streaming::ConstBuffer leafBuf(buffer, buffer.get() + leafRef.pos, buffer.get() + file.size());
                    Leaf leaf = readLeaf(leafBuf, leafRef.cheapHash);
                    const uint64_t cheapHash = leaf.txid.GetCheapHash();
                    const uint32_t leafShorthash = createShortHash(cheapHash);
                    if (shorthash != leafShorthash)
                        err << "Leaf found under bucket with different shorthashes " << shorthash << " != " << leafShorthash
                            << endl << "  " << QString::fromStdString(leaf.txid.GetHex()) << "-" << leaf.outIndex
                            << ", b: " << leaf.blockHeight << endl;

                    if (leaf.blockHeight > checkpoint.lastBlockHeight)
                        err << "Leaf belongs to a block newer than this checkpoint" << leaf.blockHeight << endl;
                    else if (leaf.blockHeight < checkpoint.firstBlockHeight)
                        err << "Leaf belongs to a block before this db file" << leaf.blockHeight << endl;
                }

                for (size_t n = 0; n < leafRefs.size(); ++n) {
                    const int leafPos = leafRefs.at(n).pos;
                    if (leafPos > checkpoint.positionInFile)  continue;
                    Streaming::ConstBuffer leafBuf(buffer, buffer.get() + leafPos, buffer.get() + file.size());
                    Leaf leaf = readLeaf(leafBuf, leafRefs.at(n).cheapHash);
                    if (leaf.txid == uint256())
                        continue;
                    for (size_t m = n + 1; m < leafRefs.size(); ++m) {
                        const int leafPos2 = leafRefs.at(m).pos;
                        Streaming::ConstBuffer leafBuf2(buffer, buffer.get() + leafPos2, buffer.get() + file.size());
                        Leaf leaf2 = readLeaf(leafBuf2, leafRefs.at(m).cheapHash);
                        if (leaf.outIndex == leaf2.outIndex && leaf.txid == leaf2.txid) {
                            err << "One utxo-entry is duplicated. " << QString::fromStdString(leaf.txid.GetHex())
                                << " | " << leaf.outIndex;
                        }
                    }
                }
            }
            out << endl;
        }
    }
    out << "Check finished" << endl;

    return Flowee::Ok;
}
