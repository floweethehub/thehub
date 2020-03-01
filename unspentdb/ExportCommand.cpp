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
#include "ExportCommand.h"

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/filesystem.hpp>
#include <qfile.h>

static void nothing(const char *){}

ExportCommand::ExportCommand()
    : m_filename(QStringList() << "o" << "output", "The [FILE] to output to", "FILE")
{
}

ExportCommand::~ExportCommand()
{
    delete m_outStream;
    delete m_device;
}

QString ExportCommand::commandDescription() const
{
    return "Export\nExports the database to either stdout or to a file.";
}

void ExportCommand::addArguments(QCommandLineParser &commandLineParser)
{
    commandLineParser.addOption(m_filename);
}

void ExportCommand::write(const AbstractCommand::Leaf &leaf)
{
    if (m_outStream == nullptr) {
        if (commandLineParser().isSet(m_filename)) {
            QString filename = commandLineParser().value(m_filename);
            QFile *file = new QFile(filename);
            if (!file->open(QIODevice::WriteOnly)) {
                err << "Failed to open out file" << endl;
                delete file;
            } else {
                m_device = file;
                m_outStream = new QTextStream(m_device);
            }
        }

        if (m_outStream == nullptr)
            m_outStream = new QTextStream(stdout);
        *m_outStream << "# txid,outindex,blockheight,offsetinblock" << endl;
    }
    QTextStream &out = *m_outStream;

    out << QString::fromStdString(leaf.txid.GetHex());
    out << "," << leaf.outIndex;
    out << "," << leaf.blockHeight;
    out << "," << leaf.offsetInBlock << endl;
}

Flowee::ReturnCodes ExportCommand::run()
{
    if (dbDataFiles().length() != 1
            || dbDataFiles().first().databaseFiles().length() != 1) {
        err << "Please select exactly one database file" << endl;
        return Flowee::InvalidOptions;
    }
    DatabaseFile infoFile = dbDataFiles().first();
    if (infoFile.filetype() != InfoFile) {
        // find the highest info file to use
        int highest = 0;
        foreach (auto info, infoFile.infoFiles()) {
            const auto checkpoint = readInfoFile(info.filepath());
            if (checkpoint.lastBlockHeight > highest) {
                infoFile = info;
                highest = checkpoint.lastBlockHeight;
            }
        }
    }

    // get data from info file.
    const auto checkpoint = readInfoFile(infoFile.filepath());
    if (checkpoint.jumptableFilepos < 0)
        return Flowee::CommandFailed;
    uint32_t jumptables[0x100000];
    if (!readJumptables(infoFile.filepath(), checkpoint.jumptableFilepos, jumptables))
        return Flowee::CommandFailed;
    if (checkpoint.jumptableHash != calcChecksum(jumptables))
        out << "Checkpoint CHECKSUM Failed" << endl;

    const DatabaseFile db = infoFile.databaseFiles().first();
    boost::iostreams::mapped_file file;
    file.open(db.filepath().toStdString(), std::ios_base::binary | std::ios_base::in);
    if (!file.is_open()) {
        err << "Failed to open db file " << db.filepath() << endl;
        return Flowee::CommandFailed;
    }
    std::shared_ptr<char> buffer = std::shared_ptr<char>(const_cast<char*>(file.const_data()), nothing);

    // read buckets
    for (int shorthash = 0; shorthash < 0x100000; ++shorthash) {
        if (jumptables[shorthash] == 0)
            continue;
        int32_t bucketOffsetInFile = static_cast<int>(jumptables[shorthash]);
        Streaming::ConstBuffer buf(buffer, buffer.get() + bucketOffsetInFile, buffer.get() + file.size());
        std::vector<LeafRef> leafPositions = readBucket(buf, bucketOffsetInFile);
        for (auto leaf : leafPositions) {
            Streaming::ConstBuffer leafBuf(buffer, buffer.get() + leaf.pos, buffer.get() + file.size());
            write(readLeaf(leafBuf, leaf.cheapHash));
        }
    }
    return Flowee::Ok;
}
