/*
 * This file is part of the Flowee project
 * Copyright (C) 2018-2021 Tom Zander <tom@flowee.org>
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
#include "PruneCommand.h"

#include <utxo/Pruner_p.h>
#include <utxo/UnspentOutputDatabase_p.h>

#include <QFileInfo>
#include <QDir>
#include <boost/filesystem.hpp>

PruneCommand::PruneCommand()
    : m_force(QStringList() << "force", "Force pruning"),
      m_backup(QStringList() << "keep" << "k", "Keep a backup file")
{
}

QString PruneCommand::commandDescription() const
{
    return "Prune\nTakes the selected database file and prunes already spent outputs";
}

void PruneCommand::addArguments(QCommandLineParser &parser)
{
    parser.addOption(m_force);
    parser.addOption(m_backup);
}

Flowee::ReturnCodes PruneCommand::run()
{
    DatabaseFile infoFile = dbDataFiles().first();
    if (dbDataFiles().size() > 1) {
        err << "Wholesale pruning is not yet possible" << endl;
        return Flowee::InvalidOptions;
    }
    else if (infoFile.filetype() == InfoFile) {
        if (!commandLineParser().isSet(m_force)) {
            err << "You selected a specific info file instead of a database\n"
                   "this risks you might not use the latest version.\n\n"
                   "Select db file instead or pass --force if you don't mind losing data" << endl;
            return Flowee::NeedForce;
        }
    }
    else if (infoFile.filetype() == Datadir) {
        err << "Whole datadir pruning is not yet possible" << endl;
        return Flowee::InvalidOptions;
    }
    else {
        // we need to find the info file with the highest blockheight;
        // the cache takes a filename like the database, but without extensions
        QString path = infoFile.filepath();
        if (path.endsWith(".db"))
            path = path.mid(0, path.length() - 3);
        DataFileCache cache(path.toStdString());
        int highest = 0, index = -1;
        for (DataFileCache::InfoFile info : cache.m_validInfoFiles) {
            if (info.lastBlockHeight > highest) {
                highest = info.lastBlockHeight;
                index = info.index;
            }
        }
        for (auto &info : infoFile.infoFiles()) {
            if (info.index() == index) {
                infoFile = info;
                break;
            }
        }
    }
    QFileInfo info(infoFile.filepath());
    if (!info.exists()) {
        err << "Failed to find an appropriate info file" << endl;
        return Flowee::InvalidOptions;
    }
    QFileInfo dbInfo(infoFile.databaseFiles().at(0).filepath());
    out << "Operating on " << dbInfo.fileName() << " and snapshot file " << info.fileName() << endl;

    try {
        Pruner pruner(dbInfo.absoluteFilePath().toStdString(), info.absoluteFilePath().toStdString());
        pruner.prune();

        out << "Finishing up" << endl;
        // backup original files.
        QFile::rename(dbInfo.absoluteFilePath(), dbInfo.absoluteFilePath() + "~");
        QFile::rename(info.absoluteFilePath(), info.absoluteFilePath() + "~");

        // remove all old info files (they can no longer work) and rename the db file
        // and the info file over the original ones
        DataFileCache cache(dbInfo.dir().filePath(dbInfo.baseName()).toStdString());
        for (int i = 0; i < 20; ++i)
            boost::filesystem::remove(cache.filenameFor(i));

        pruner.commit();
        fflush(nullptr);

        if (!commandLineParser().isSet(m_backup)) {
            QFile::remove(dbInfo.absoluteFilePath() + "~");
            QFile::remove(info.absoluteFilePath() + "~");
        }
        out << "Done" << endl;
        return Flowee::Ok;
    } catch (const std::runtime_error &ex) {
        err << ex.what() << endl;
        return Flowee::CommandFailed;
    }
}
