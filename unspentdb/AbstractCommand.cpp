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
#include "AbstractCommand.h"
#include <Logger.h>
// private header as we read the db tags directly
#include <utxo/UnspentOutputDatabase_p.h>

#include <QCommandLineParser>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <hash.h>

#include <streaming/MessageParser.h>

AbstractCommand::AbstractCommand()
    : out(stdout),
      err(stderr)
{
    Log::Manager::instance()->clearLogLevels(Log::InfoLevel);
}

AbstractCommand::~AbstractCommand()
{
}

Flowee::ReturnCodes AbstractCommand::start(const QStringList &args)
{
    m_parser.setApplicationDescription(commandDescription());
    m_parser.addHelpOption();
    addArguments(m_parser);
    m_parser.process(args);

    auto positionalArguments = m_parser.positionalArguments();
    const auto rc = preParseArguments(positionalArguments);
    if (rc != Flowee::Ok)
        return rc;
    for (auto fn : positionalArguments) {
        DBFileType ft = Unknown;
        if (fn.endsWith(".info"))
            ft = InfoFile;
        else if (fn.endsWith(".db"))
            ft = DBFile;
        else if (QFileInfo(fn).isDir())
            ft = Datadir;
        else {
            err << "Don't know what to do with arg:" << fn;
            return Flowee::InvalidOptions;
        }
        m_dataFiles << DatabaseFile(fn, ft);
    }

    if (m_dataFiles.isEmpty())
        m_parser.showHelp();

    return run();
}

void AbstractCommand::addArguments(QCommandLineParser &)
{
}

QList<AbstractCommand::DatabaseFile> AbstractCommand::dbDataFiles() const
{
    return m_dataFiles;
}

QList<AbstractCommand::DatabaseFile> AbstractCommand::highestDataFiles()
{
    QList<DatabaseFile> answer;
    for (auto df : m_dataFiles) {
        if (df.filetype() == InfoFile)
            return answer << df;

        for (auto db : df.databaseFiles()) {
            // TODO sync checkpoint-versions between datafiles
            AbstractCommand::DatabaseFile infoFile;
            int highest = 0;
            foreach (auto info, db.infoFiles()) {
                const auto checkpoint = readInfoFile(info.filepath());
                if (checkpoint.lastBlockHeight > highest) {
                    infoFile = info;
                    highest = checkpoint.lastBlockHeight;
                }
            }
            if (highest != 0)
                answer.append(infoFile);
        }
    }

    return answer;
}

QCommandLineParser &AbstractCommand::commandLineParser()
{
    return m_parser;
}


/////////////////////////////////////

AbstractCommand::DatabaseFile::DatabaseFile()
    : m_filetype(Unknown)
{
}

AbstractCommand::DatabaseFile::DatabaseFile(const QString &filepath, AbstractCommand::DBFileType filetype, int index)
    : m_filepath(filepath),
      m_filetype(filetype),
      m_index(index)
{
}

QString AbstractCommand::DatabaseFile::filepath() const
{
    return m_filepath;
}

AbstractCommand::DBFileType AbstractCommand::DatabaseFile::filetype() const
{
    return m_filetype;
}

int AbstractCommand::DatabaseFile::index() const
{
    return m_index;
}

QList<AbstractCommand::DatabaseFile> AbstractCommand::DatabaseFile::infoFiles() const
{
    QList<DatabaseFile> answer;
    if (m_filetype == InfoFile) {
        answer.append(*this);
    }
    else if (m_filetype == DBFile) {
        const QFileInfo dbInfo(m_filepath);
        QString templateName = dbInfo.fileName().remove(".db");
        templateName += ".%1.info";
        for (int i = 0; i < 20; ++i) {
            QFileInfo info(dbInfo.absoluteDir(), templateName.arg(i));
            if (info.exists())
                answer += DatabaseFile(info.absoluteFilePath(), InfoFile, i);
        }
    }
    else {
        foreach (auto dbf, databaseFiles()) {
            answer.append(dbf.infoFiles());
        }
    }
    return answer;
}

QList<AbstractCommand::DatabaseFile> AbstractCommand::DatabaseFile::databaseFiles() const
{
    QList<DatabaseFile> answer;
    if (m_filetype == Datadir) {
        const QDir dir(m_filepath);
        QString templateName("data-%1.db");
        for (int i = 1; i < 1000; ++i) {
            QFileInfo info(dir, templateName.arg(i));
            if (!info.exists())
                break;
            answer += DatabaseFile(info.absoluteFilePath(), DBFile, i);
        }
    }
    else if (m_filetype == InfoFile && m_filepath.endsWith(".info")) {
        int index = m_filepath.lastIndexOf(".", -6);
        if (index > 0) {
            answer.append(DatabaseFile(m_filepath.left(index) + ".db", DBFile));
        }
    }
    else if (m_filetype == DBFile) {
        answer.append(*this);
    }
    return answer;
}

bool AbstractCommand::readJumptables(const QString &filepath, int startPos, uint32_t *tables)
{
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    file.seek(startPos);
    auto bytesRead = file.read(reinterpret_cast<char*>(tables), 0x400000);
    if (bytesRead <= 0) {
        err << "Jumptable not present or file could not be read";
        return false;
    }
    if (bytesRead < 0x400000) {
        err << "Hashtable truncated, expected " << 0x400000 << " bytes, got " << bytesRead << endl;
        return false;
    }
    return true;
}

uint256 AbstractCommand::calcChecksum(uint32_t *tables) const
{
    CHash256 ctx;
    ctx.Write(reinterpret_cast<const unsigned char*>(tables), 0x400000);
    uint256 checksum;
    ctx.Finalize(reinterpret_cast<unsigned char*>(&checksum));
    return checksum;
}


AbstractCommand::CheckPoint AbstractCommand::readInfoFile(const QString &filepath)
{
    CheckPoint checkpoint;
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)) {
        err << "Can't open file " << filepath << endl;
        return checkpoint;
    }
    Streaming::BufferPool pool(500);
    qint64 read = file.read(pool.begin(), 500);
    Streaming::MessageParser parser(pool.commit(read));
    Streaming::ParsedType type = parser.next();
    while (type == Streaming::FoundTag) {
        switch (static_cast<UODB::MessageTags>(parser.tag())) {
        case UODB::IsTip:
            checkpoint.isTip = parser.boolData();
            break;
        case UODB::InvalidBlockHash:
            if (!parser.isByteArray())
                err << "invalidBlockHash not a bytearray";
            else if (parser.dataLength() != 32)
                err << "invalidBlockHash not a sha256";
            else
                checkpoint.invalidBlockHashes.push_back(parser.uint256Data());
            break;
        case UODB::ChangesSincePrune:
            checkpoint.changesSincePrune = parser.intData();
            break;
        case UODB::InitialBucketSegmentSize:
            checkpoint.initialBucketSize = parser.intData();
            break;
        case UODB::Separator:
            checkpoint.jumptableFilepos = parser.consumed();
            return checkpoint;
        case UODB::LastBlockId:
            checkpoint.lastBlockId = parser.uint256Data();
            break;
        case UODB::FirstBlockHeight:
            checkpoint.firstBlockHeight = parser.longData();
            break;
        case UODB::LastBlockHeight:
            checkpoint.lastBlockHeight = parser.longData();
            break;
        case UODB::JumpTableHash:
            checkpoint.jumptableHash = parser.uint256Data();
            break;
        case UODB::PositionInFile:
            checkpoint.positionInFile = parser.longData();
            break;

        case UODB::LeafPosOn512MB:
        case UODB::LeafPosFromPrevLeaf:
        case UODB::LeafPosRepeat:
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
        type = parser.next();
    }
    return checkpoint;
}

AbstractCommand::Leaf AbstractCommand::readLeaf(Streaming::ConstBuffer buf, quint64 cheapHash, bool *failed)
{
    Leaf answer;
    if (failed) *failed = false;
    Streaming::MessageParser parser(buf);
    Streaming::ParsedType type = parser.next();
    bool hitSeparator = false;
    while (type == Streaming::FoundTag) {
        if (parser.tag() == UODB::BlockHeight) {
            if (!parser.isInt()) {
                if (failed) *failed = true;
                else err << "Tag mismatch, blockheight should be an int" << endl;
                break;
            }
            answer.blockHeight = parser.intData();
        }
        else if (parser.tag() == UODB::OffsetInBlock) {
            if (!parser.isInt()) {
                if (failed) *failed = true;
                else err << "Tag mismatch, offsetInBlock should be an int" << endl;
                break;
            }
            answer.offsetInBlock = parser.intData();
        } else if (!hitSeparator && parser.tag() == UODB::OutIndex) {
            if (!parser.isInt()) {
                if (failed) *failed = true;
                else err << "Tag mismatch, outIndex should be an int" << endl;
                break;
            }
            answer.outIndex = parser.intData();
            if (!failed && answer.outIndex == 0)
                err << "Warn; outindex saved while zero" << endl;
        } else if (parser.tag() == UODB::TXID) {
            if (!parser.isByteArray() || (parser.dataLength() != 32 && parser.dataLength() != 24)) {
                if (failed) *failed = true;
                else err << "Tag mismatch, txid should be a 32 or a 24 byte bytearray" << endl;
                break;
            }
            if (parser.dataLength() == 32)
                answer.txid = parser.uint256Data();
            else {
                char fullHash[32];
                WriteLE64(reinterpret_cast<unsigned char*>(fullHash), cheapHash);
                memcpy(fullHash + 8, parser.bytesData().data(), 24);
                answer.txid = uint256(fullHash);
            }
        } else if (parser.tag() == UODB::Separator)
            hitSeparator = true;
        if (hitSeparator && !answer.txid.IsNull())
            break;
        type = parser.next();
    }
    if (type == Streaming::Error) {
        if (failed) *failed = true;
        else err << "CMF Parse error in reading leaf" << endl;
    }
    return answer;
}

std::vector<AbstractCommand::LeafRef> AbstractCommand::readBucket(Streaming::ConstBuffer buf, int bucketOffsetInFile, bool *failed)
{
    std::vector<LeafRef> answer;
    Streaming::MessageParser parser(buf);
    quint64 cheapHash = 0;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == UODB::CheapHash)
            cheapHash = parser.longData();
        else if (parser.tag() == UODB::LeafPosRelToBucket) {
            int offset = parser.intData();
            if (offset > bucketOffsetInFile) {
                if (failed) *failed = true;
                else err << "Error found. Offset to bucket leads to negative file position." << endl;
            }
            else
                answer.push_back({cheapHash, bucketOffsetInFile - offset});
        }
        else if (parser.tag() == UODB::LeafPosition) {
            answer.push_back({cheapHash, parser.intData()});
        } else if (parser.tag() == UODB::LeafPosOn512MB) {
            answer.push_back({cheapHash, 512 * 1024 * 1024 + parser.intData()});
        } else if (parser.tag() == UODB::LeafPosFromPrevLeaf) {
            if (answer.empty()) {
                if (failed) *failed = true;
                else err << "Error found. LeafPosFroMPrevLeaf used for first leaf in bucket." << endl;
            }
            else {
                answer.push_back({cheapHash, answer.back().pos - parser.intData()});
            }
        } else if (parser.tag() == UODB::Separator) {
            break;
        }
    }
    return answer;
}
