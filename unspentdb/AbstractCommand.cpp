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
}

AbstractCommand::~AbstractCommand()
{
}

Flowee::ReturnCodes AbstractCommand::start(const QStringList &args)
{
    m_parser.setApplicationDescription(commandDescription());
    QCommandLineOption datafile(QStringList() << "f" << "datafile", "<PATH> to datafile.db.", "PATH");
    QCommandLineOption basedir(QStringList() << "d" << "unspent", "<PATH> to unspent datadir.", "PATH");
    QCommandLineOption infoFile(QStringList() << "i" << "info", "<PATH> to specific info file.", "PATH");
    m_parser.addOption(datafile);
    m_parser.addOption(basedir);
    m_parser.addOption(infoFile);
    m_parser.addHelpOption();
    addArguments(m_parser);
    m_parser.process(args);

    if (m_parser.isSet(datafile)) {
        m_data = DatabaseFile(m_parser.value(datafile), DBFile);
    }
    if (m_parser.isSet(basedir)) {
        if (m_data.filetype() != Unknown) {
            err << "You can only pass in one of --datafile, --unspent or --info" << endl;
            return Flowee::InvalidOptions;
        }
        m_data = DatabaseFile(m_parser.value(basedir), Datadir);
    }
    if (m_parser.isSet(infoFile)) {
        if (m_data.filetype() != Unknown) {
            err << "You can only pass in one of --datafile, --unspent or --info" << endl;
            return Flowee::InvalidOptions;
        }
        m_data = DatabaseFile(m_parser.value(infoFile), InfoFile);
    }
    if (m_data.filetype() == Unknown)
        m_parser.showHelp();

    return run();
}

void AbstractCommand::addArguments(QCommandLineParser &)
{
}

AbstractCommand::DatabaseFile AbstractCommand::dbDataFile() const
{
    return m_data;
}

const QCommandLineParser &AbstractCommand::commandLineParser() const
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
        for (int i = 0; i < 10; ++i) {
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

bool AbstractCommand::readJumptabls(const QString &filepath, int startPos, uint32_t *tables)
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

AbstractCommand::Leaf AbstractCommand::readLeaf(Streaming::ConstBuffer buf, bool *failed)
{
    Leaf answer;
    if (failed) *failed = false;
    Streaming::MessageParser parser(buf);
    Streaming::ParsedType type = parser.next();
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
        } else if (parser.tag() == UODB::OutIndex) {
            if (!parser.isInt()) {
                if (failed) *failed = true;
                else err << "Tag mismatch, outIndex should be an int" << endl;
                break;
            }
            answer.outIndex = parser.intData();
            if (!failed && answer.outIndex == 0)
                err << "Warn; outindex saved while zero" << endl;
        } else if (parser.tag() == UODB::TXID) {
            if (!parser.isByteArray() || parser.dataLength() != 32) {
                if (failed) *failed = true;
                else err << "Tag mismatch, txid should be a 32 byte bytearray" << endl;
                break;
            }
            answer.txid = parser.uint256Data();
        } else if (parser.tag() == UODB::Separator)
            break;
        type = parser.next();
    }
    if (type == Streaming::Error) {
        if (failed) *failed = true;
        else err << "CMF Parse error in reading bucket" << endl;
    }
    return answer;
}

std::vector<int> AbstractCommand::readBucket(Streaming::ConstBuffer buf, int bucketOffsetInFile, bool *failed)
{
    std::vector<int> answer;
    Streaming::MessageParser parser(buf);
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == UODB::LeafPosRelToBucket) {
            int offset = parser.intData();
            if (offset > bucketOffsetInFile) {
                if (failed) *failed = true;
                else err << "Error found. Offset to bucket leads to negative file position." << endl;
            }
            else
                answer.push_back(bucketOffsetInFile - offset);
        }
        else if (parser.tag() == UODB::LeafPosition) {
            answer.push_back(parser.intData());
        } else if (parser.tag() == UODB::Separator) {
            break;
        }
    }
    return answer;
}
