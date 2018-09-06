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

#include <QCommandLineParser>

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
    QCommandLineParser parser;
    parser.setApplicationDescription(commandDescription());
    QCommandLineOption datafile(QStringList() << "f" << "datafile", "<PATH> to datafile.db.", "PATH");
    QCommandLineOption basedir(QStringList() << "d" << "unspent", "<PATH> to unspent datadir.", "PATH");
    QCommandLineOption infoFile(QStringList() << "i" << "info", "<PATH> to specific info file.", "PATH");
    parser.addOption(datafile);
    parser.addOption(basedir);
    parser.addOption(infoFile);
    parser.addHelpOption();
    addArguments(parser);
    parser.process(args);

    if (parser.isSet(datafile)) {
        m_data.append(DatabaseFile(parser.value(datafile), DBFile));
    }
    if (parser.isSet(basedir)) {
        if (!m_data.isEmpty()) {
            err << "You can only pass in one of --datafile, --unspent or --info" << endl;
            return Flowee::InvalidOptions;
        }
        m_data.append(DatabaseFile(parser.value(basedir), Datadir));
    }
    if (parser.isSet(infoFile)) {
        if (!m_data.isEmpty()) {
            err << "You can only pass in one of --datafile, --unspent or --info" << endl;
            return Flowee::InvalidOptions;
        }
        m_data.append(DatabaseFile(parser.value(infoFile), InfoFile));
    }
    if (m_data.isEmpty())
        parser.showHelp();

    return run();
}

void AbstractCommand::addArguments(QCommandLineParser &parser)
{
}

QList<AbstractCommand::DatabaseFile> AbstractCommand::dbDataFiles() const
{
    return m_data;
}


/////////////////////////////////////

AbstractCommand::DatabaseFile::DatabaseFile(const QString &filepath, AbstractCommand::DBFileType filetype)
    : m_filepath(filepath),
      m_filetype(filetype)
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
