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
#include "LookupCommand.h"

// private header for createShortHash()
#include <utxo/UnspentOutputDatabase_p.h>

#include <primitives/FastTransaction.h>
#include <server/dbwrapper.h>

#include <QDir>
#include <QFileInfo>

// from libs/server
#include <chain.h>

#include <boost/scoped_ptr.hpp>


static void nothing(const char *){}

namespace {
static const char DB_BLOCK_INDEX = 'b';
class BlocksDB : public CDBWrapper
{
public:
    BlocksDB(const boost::filesystem::path& path)
        : CDBWrapper(path / "blocks" / "index", 1000, false, false)
    {
    }

    bool findBlock(int blockHeight, int &rFile, int &rPos)
    {
        boost::scoped_ptr<CDBIterator> pcursor(NewIterator());
        pcursor->Seek(std::make_pair(DB_BLOCK_INDEX, uint256()));
        while (pcursor->Valid()) {
            std::pair<char, uint256> key;
            if (pcursor->GetKey(key) && key.first == DB_BLOCK_INDEX) {
                CDiskBlockIndex diskindex;
                if (pcursor->GetValue(diskindex)) {
                    if (diskindex.nHeight == blockHeight && (diskindex.nStatus & BLOCK_FAILED_MASK) == 0) {
                        rFile = diskindex.nFile;
                        rPos = diskindex.nDataPos;
                        return true;
                    }
                    pcursor->Next();
                } else {
                    return false;
                }
            } else {
                break;
            }
        }
        return false;
    }
};
}


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

void LookupCommand::findTransaction(const AbstractCommand::Leaf &leaf)
{
    QFileInfo info(dbDataFiles().first().filepath());
    QDir dir = info.absoluteDir();
    dir.cdUp();
    BlocksDB db(dir.absolutePath().toStdString());
    int fileno, blockPos;
    if (db.findBlock(leaf.blockHeight, fileno, blockPos)) {
        dir.cd("blocks");
        QFile file(dir.filePath(QString("blk%1.dat").arg(QString::number(fileno), 5, QLatin1Char('0'))));
        if (!file.open(QIODevice::ReadOnly)) {
            err << "Failed to open block file" << file.fileName() << endl;
            return;
        }
        if (!file.seek(blockPos - 4)) {
            err << "Block file too small" << endl;
            return;
        }
        QByteArray blockSizeBytes = file.read(4);
        uint32_t blockSize = le32toh(*(reinterpret_cast<const std::uint32_t*>(blockSizeBytes.data())));
        if (leaf.offsetInBlock >= blockSize) {
            err << "Block smaller than offset of transaction" << endl;
            return;
        }
        if (!file.seek(blockPos + leaf.offsetInBlock)) {
            err << "Seek failed to move to transaction pos" << endl;
            return;
        }
        const int size = blockSize - leaf.offsetInBlock;
        Streaming::BufferPool pool(size);
        file.read(pool.begin(), size);
        Tx tx(pool.commit(size));
        Tx::Output output = tx.output(leaf.outIndex);
        if (output.outputScript.size() == 0) {
            err << "Could not find the output";
            return;
        }
        out << " +- Value: " << output.outputValue << " sat" << endl;
        out << " +- Script: 0x";
        for (int i = 0; i < output.outputScript.size(); ++i) {
            QString hex = QString::number(output.outputScript[i], 16);
            if (hex.length() < 2)
                out << "0";
            out << hex;
        }
        out << endl;
        out << endl;
    }
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
    if (commandLineParser().isSet(m_all)) {
        for (auto dbFile : dbDataFiles()) {
            files.append(dbFile.infoFiles());
        }
    } else {
        files = highestDataFiles();
    }

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
        std::vector<LeafRef> leafs;
        if (filePos >= 0) {
            leafs.push_back({0, filePos});
        }
        else if (bucketOffsetInFile) {
            if (debug)
                out << "File has appropriate bucket " << db.filepath() << endl;
            Streaming::ConstBuffer buf(buffer, buffer.get() + bucketOffsetInFile, buffer.get() + file.size());
            leafs = readBucket(buf, bucketOffsetInFile);
        }
        bool foundOne = false;
        for (auto leafRef : leafs) {
            Streaming::ConstBuffer leafBuf(buffer, buffer.get() + leafRef.pos, buffer.get() + file.size());
            if (debug)
                out << " + checking leaf at filepos: " << leafRef.pos << endl;
            Leaf leaf = readLeaf(leafBuf, leafRef.cheapHash);
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
                    out << "  Leaf file offset: " << leafRef.pos << endl;
                }

                findTransaction(leaf);
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
