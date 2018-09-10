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
#include "LookupCommand.h"

// private header for createShortHas()
#include <utxo/UnspentOutputDatabase_p.h>

static void nothing(const char *){}

LookupCommand::LookupCommand()
    : m_printDebug(QStringList() << "v" << "debug", "Print internal DB details"),
    m_all(QStringList() << "a" << "all", "Use historical checkpoints as well"),
    m_filepos(QStringList() << "filepos", "Lookup and print the leaf at a specific file [pos]", "pos")
{
}

QString LookupCommand::commandDescription() const
{
    return "Lookup\nCheck and print if a certain utxo entry is available";
}

void LookupCommand::addArguments(QCommandLineParser &commandLineParser)
{
    commandLineParser.addPositionalArgument("txid", "Transaction ID");
    commandLineParser.addPositionalArgument("output", "index of the output");
    commandLineParser.addOption(m_printDebug);
    commandLineParser.addOption(m_all);
    commandLineParser.addOption(m_filepos);
}

Flowee::ReturnCodes LookupCommand::run()
{
    QStringList args = commandLineParser().positionalArguments();
    if (args.isEmpty()) {
        commandLineParser().showHelp();
        return Flowee::InvalidOptions;
    }
    uint256 hash;
    hash.SetHex(args.first().toLatin1().constData());
    int outindex = -1;
    if (args.size() > 1) {
        bool ok;
        outindex = args.at(1).toInt(&ok);
        if (!ok || outindex < 0) {
            err << "second argument is the out, index. Which should be a positive number." << endl;
            return Flowee::InvalidOptions;
        }
    }

    out << "Searching for " << QString::fromStdString(hash.GetHex()) << endl;

    const uint64_t cheapHash = hash.GetCheapHash();
    const uint32_t shortHash = createShortHash(cheapHash);
    const bool debug = commandLineParser().isSet(m_printDebug);
    if (debug)
        out << "cheapHash: " << cheapHash << ", shortHash: " << shortHash << endl;

    int filePos = -1;
    if (commandLineParser().isSet(m_filepos)) {
        bool ok;
        filePos = commandLineParser().value(m_filepos).toInt(&ok);
        if (!ok << filePos < 0) {
            err << "Filepos has to be a positive number" << endl;
            return Flowee::InvalidOptions;
        }
    }

    QList<DatabaseFile> files;
    if (commandLineParser().isSet(m_all))
        files = dbDataFile().infoFiles();
    else
        files = highestDataFiles();
    for (auto info : files) {
        if (debug)
            out << "Opening " << info.filepath() << endl;
        const auto checkpoint = readInfoFile(info.filepath());
        if (checkpoint.jumptableFilepos < 0) {
            err << "failed parsing " << info.filepath() << endl;
            continue;
        }
        uint32_t jumptables[0x100000];
        if (!readJumptables(info.filepath(), checkpoint.jumptableFilepos, jumptables)) {
            err << "failed parsing(2) " << info.filepath() << endl;
            continue;
        }
        if (checkpoint.jumptableHash != calcChecksum(jumptables)) {
            err << "failed parsing(3) " << info.filepath() << endl;
            continue;
        }

        const DatabaseFile db = info.databaseFiles().first();
        boost::iostreams::mapped_file file;
        file.open(db.filepath().toStdString(), std::ios_base::binary | std::ios_base::in);
        if (!file.is_open()) {
            err << "failed parsing(4) " << info.filepath() << endl;
            continue;
        }
        std::shared_ptr<char> buffer = std::shared_ptr<char>(const_cast<char*>(file.const_data()), nothing);

        const int32_t bucketOffsetInFile = static_cast<int>(jumptables[shortHash]);
        std::vector<int> leafs;
        if (filePos >= 0) {
            leafs.push_back(filePos);
        }
        else if (bucketOffsetInFile) {
            if (debug)
                out << "File has appropriate bucket " << db.filepath() << endl;
            Streaming::ConstBuffer buf(buffer, buffer.get() + bucketOffsetInFile, buffer.get() + file.size());
            leafs = readBucket(buf, bucketOffsetInFile);
        }
        bool foundOne = false;
        for (auto pos : leafs) {
            Streaming::ConstBuffer leafBuf(buffer, buffer.get() + pos, buffer.get() + file.size());
            if (debug)
                out << " + checking leaf at filepos: " << pos << endl;
            Leaf leaf = readLeaf(leafBuf);
            if (leaf.txid == hash && (outindex == -1 || outindex == leaf.outIndex)) {
                if (!foundOne) {
                    out << "In UTXO up to block height: " << checkpoint.lastBlockHeight << " (" <<
                        QString::fromStdString(checkpoint.lastBlockId.GetHex()) << ")" << endl;
                    if (debug)
                        out << "In DB file " << db.filepath() << endl;
                }
                foundOne = true;
                out << "Entry is unspent; " << QString::fromStdString(leaf.txid.GetHex()) << "-"
                    << leaf.outIndex << endl;
                if (debug) {
                    out << "  tx is in block " << leaf.blockHeight << ", tx is at bytepos in block: " << leaf.offsetInBlock << endl;
                    out << "  Leaf file offset: " << pos << endl;
                }
            }
            if (foundOne)
                return Flowee::Ok;
            else if (debug && filePos >= 0) {
                out << "Recoverable data:" << endl
                    << "  TXID: " << QString::fromStdString(leaf.txid.GetHex()) << "-" << leaf.outIndex << endl
                    << "  Block height: " << leaf.blockHeight << ", offset in block: " << leaf.offsetInBlock << endl << endl;
            }
        }
    }
    return Flowee::CommandFailed;
}
