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
    : m_printDebug(QStringList() << "v" << "debug", "Print internal DB details")
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

    for (auto info : highestDataFiles()) {
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
        if (jumptables[shortHash]) {
            if (debug)
                out << "File has appropriate bucket " << db.filepath() << endl;
            int32_t bucketOffsetInFile = static_cast<int>(jumptables[shortHash]);
            Streaming::ConstBuffer buf(buffer, buffer.get() + bucketOffsetInFile, buffer.get() + file.size());
            bool foundOne = false;
            for (auto pos : readBucket(buf, bucketOffsetInFile)) {
                Streaming::ConstBuffer leafBuf(buffer, buffer.get() + pos, buffer.get() + file.size());
                Leaf leaf = readLeaf(leafBuf);
                if (debug)
                    out << " + checking leaf at filepos: " << pos << endl;
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
                    if (debug)
                        out << "  tx is in block " << leaf.blockHeight << ", tx is at bytepos in block: " << leaf.offsetInBlock << endl;
                }
            }
            if (foundOne)
                return Flowee::Ok;
        }
    }
    return Flowee::CommandFailed;
}
