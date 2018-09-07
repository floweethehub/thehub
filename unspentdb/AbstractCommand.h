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
#ifndef ABSTRACTCOMMAND_H
#define ABSTRACTCOMMAND_H

#include <QString>
#include <QTextStream>

class QCommandLineParser;

namespace Flowee
{
    enum ReturnCodes {
        // note that Unix requires 'Ok' to be zero.
        Ok = 0,
        InvalidOptions = 1,
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
        DatabaseFile(const QString &filepath, DBFileType filetype);
        DatabaseFile(const DatabaseFile &other) = default;

        QList<DatabaseFile> infoFiles() const;
        QList<DatabaseFile> databaseFiles() const;

        QString filepath() const;
        DBFileType filetype() const;

    private:
        QString m_filepath;
        DBFileType m_filetype;
    };

    virtual void addArguments(QCommandLineParser &parser);

    /**
     * return long (multiline) Description of the command.
     */
    virtual QString commandDescription() const = 0;

    /**
     * Run the commmand
     * @return the exit code as used by the comamnd.
     */
    virtual Flowee::ReturnCodes run() = 0;

    DatabaseFile dbDataFile() const;

    QTextStream out, err;

private:
    DatabaseFile m_data;
};

#endif
