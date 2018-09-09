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
            out << "CHECKSUM Failed";
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

        // open db file
        boost::iostreams::mapped_file file;
        file.open(dbs.first().filepath().toStdString(), std::ios_base::binary | std::ios_base::in);
        if (!file.is_open()) {
            err << "Failed to open db file" << endl;
            continue;
        }
        std::shared_ptr<char> buffer = std::shared_ptr<char>(const_cast<char*>(file.const_data()), nothing);

        // read buckets
        for (int shorthash = 0; shorthash < 0x100000; ++shorthash) {
            if (jumptables[shorthash] == 0)
                continue;
            int32_t bucketOffsetInFile = static_cast<int>(jumptables[shorthash]);

            Streaming::ConstBuffer buf(buffer, buffer.get() + bucketOffsetInFile, buffer.get() + file.size());
            Streaming::MessageParser parser(buf);

        }
    }

    /*
     * open each info file and check if its checksum passes.
     * remember the highest disk position
     * then walk through the table and check nothing has a higher offset-in-file
     *
     * checkBucket(shortHash, diskPos);
     *   read bucket
     *   read each leaf
     *      check if the shorthash is consistent
     *      check if we have any duplicate leafs
     *
     */
    return Flowee::Ok;
}
