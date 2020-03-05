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
#ifndef ABSTRACTCOMMAND_H
#define ABSTRACTCOMMAND_H

#include <uint256.h>

#include <QCommandLineParser>
#include <QString>
#include <QTextStream>
#include <deque>

#include <streaming/ConstBuffer.h>

class QCommandLineParser;
class QFile;

namespace Flowee
{
    enum ReturnCodes {
        // note that Unix requires 'Ok' to be zero.
        Ok = 0,
        InvalidOptions = 1,
        NeedForce = 2,
        CommandFailed = 3
    };
}

namespace Log
{
    enum Section {
        UnspentCli = 11000
    };

}

class AbstractCommand {
public:
    AbstractCommand();
    virtual ~AbstractCommand();

    Flowee::ReturnCodes start(const QStringList &args);

protected:
    enum DBFileType {
        InfoFile, // the .info file, multiple per database file
        DBFile,   // the data-n.db file, multiple in a datadir
        Datadir,   // the directory 'unspent' where all UTXO is stored
        Unknown
    };
    class DatabaseFile {
    public:
        DatabaseFile();
        DatabaseFile(const QString &filepath, DBFileType filetype, int index = -1);
        DatabaseFile(const DatabaseFile &other) = default;

        QList<DatabaseFile> infoFiles() const;
        QList<DatabaseFile> databaseFiles() const;

        QString filepath() const;
        DBFileType filetype() const;

        /// return the index if applicable. Indexes are used in info-files.
        int index() const;

    private:
        QString m_filepath;
        DBFileType m_filetype;
        int m_index = -1;
    };

    virtual void addArguments(QCommandLineParser &commandLineParser);
    virtual Flowee::ReturnCodes preParseArguments(QStringList &positionalArguments) {
        Q_UNUSED(positionalArguments)
        return Flowee::Ok;
    }

    /**
     * return long (multiline) Description of the command.
     */
    virtual QString commandDescription() const = 0;

    /**
     * Run the commmand
     * @return the exit code as used by the comamnd.
     */
    virtual Flowee::ReturnCodes run() = 0;

    QList<DatabaseFile> dbDataFiles() const;

    /**
     * Find the files representing the highest consistent version
     * in the selection of dbDataFile().
     * At most one info file is returned per datafile.
     */
    QList<DatabaseFile> highestDataFiles();

    QTextStream out, err;

    QCommandLineParser &commandLineParser();

    bool readJumptables(const QString &filepath, int startPos, uint32_t *tables);
    uint256 calcChecksum(uint32_t *tables) const;

    struct CheckPoint {
        uint256 lastBlockId;
        uint256 jumptableHash;
        int firstBlockHeight = -1;
        int lastBlockHeight = -1;
        int positionInFile = -1;
        int jumptableFilepos = -1;
        int changesSincePrune = -1;
        int initialBucketSize = -1;
        bool isTip = false;
        std::deque<uint256> invalidBlockHashes;
    };
    CheckPoint readInfoFile(const QString &filepath);


    struct Leaf {
        int blockHeight = -1;
        int offsetInBlock = -1;
        int outIndex = 0;
        uint256 txid;
    };

    Leaf readLeaf(Streaming::ConstBuffer buf, quint64 cheapHash, bool *failed = nullptr);

    struct LeafRef {
        quint64 cheapHash;
        int pos;
    };

    std::vector<LeafRef> readBucket(Streaming::ConstBuffer buf, int bucketOffsetInFile, bool *failed = nullptr);

private:
    QCommandLineParser m_parser;
    QList<DatabaseFile> m_dataFiles;
};

#endif
