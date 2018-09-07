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
#include <QFileInfo>
#include <QDir>

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
        m_data = DatabaseFile(parser.value(datafile), DBFile);
    }
    if (parser.isSet(basedir)) {
        if (m_data.filetype() != Unknown) {
            err << "You can only pass in one of --datafile, --unspent or --info" << endl;
            return Flowee::InvalidOptions;
        }
        m_data = DatabaseFile(parser.value(basedir), Datadir);
    }
    if (parser.isSet(infoFile)) {
        if (m_data.filetype() != Unknown) {
            err << "You can only pass in one of --datafile, --unspent or --info" << endl;
            return Flowee::InvalidOptions;
        }
        m_data = DatabaseFile(parser.value(infoFile), InfoFile);
    }
    if (m_data.filetype() == Unknown)
        parser.showHelp();

    return run();
}

void AbstractCommand::addArguments(QCommandLineParser &parser)
{
}

AbstractCommand::DatabaseFile AbstractCommand::dbDataFile() const
{
    return m_data;
}


/////////////////////////////////////

AbstractCommand::DatabaseFile::DatabaseFile()
    : m_filetype(Unknown)
{
}

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
                answer += DatabaseFile(info.absoluteFilePath(), InfoFile);
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
            answer += DatabaseFile(info.absoluteFilePath(), DBFile);
        }
    }
    return answer;
}
