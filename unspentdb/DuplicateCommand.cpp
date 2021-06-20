/*
 * This file is part of the Flowee project
 * Copyright (C) 2020 Tom Zander <tom@flowee.org>
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
#include "DuplicateCommand.h"

#include <QDir>
#include <QFileInfo>

DuplicateCommand::DuplicateCommand()
{
}

QString DuplicateCommand::commandDescription() const
{
    return "Duplicate\nCreates a duplicate of the database (segment) passed in the argument";
}

Flowee::ReturnCodes DuplicateCommand::run()
{
    Q_ASSERT(!m_target.isEmpty());
    const auto input = highestDataFiles();
    if (input.size() > 1) {
        // then output should be a directory.
        QFileInfo output(m_target);
        if (output.exists() && !output.isDir()) {
            err << "Output should be a directory" << endl;
            return Flowee::InvalidOptions;
        }
        else if (!output.exists()) {
            if (!QDir::current().mkpath(m_target)) {
                err << "Could not write to target: " << m_target << endl;
                return Flowee::CommandFailed;
            }
        }
    }

    for (auto info : highestDataFiles()) {
        out << "Copying " << info.filepath() << endl;
        CheckPoint checkpoint = readInfoFile(info.filepath());

        QFileInfo infoFile(info.filepath());
        const bool ok = QFile::copy(infoFile.absoluteFilePath(), m_target + "/" + infoFile.fileName());
        if (!ok) {
            err << "Failed to copy " << infoFile.absoluteFilePath() << endl;
            return Flowee::CommandFailed;
        }

        const auto db = info.databaseFiles().first();
        QFile in(db.filepath());
        if (!in.open(QIODevice::ReadOnly)) {
            err << "Failed to read from " << db.filepath() << endl;
            return Flowee::CommandFailed;
        }
        QFile out(m_target + "/" + QFileInfo(db.filepath()).fileName());
        if (!out.open(QIODevice::WriteOnly)) {
            err << "Failed to write to " << out.fileName();
            return Flowee::CommandFailed;
        }
        // copy only checkpoint.positionInFile bytes.
        char buf[100000];
        qint64 bytesToCopy = checkpoint.positionInFile;
        while (bytesToCopy > 0) {
            auto read = in.read(buf, std::min<qint64>(bytesToCopy, sizeof(buf)));
            auto written = out.write(buf, read);
            if (written == -1) {
                err << "Failed to write bytes to file: " << out.fileName();
                return Flowee::CommandFailed;
            }
            Q_ASSERT(written == read);
            bytesToCopy -= read;
        }

        in.close();
        out.resize(in.size());
    }

    return Flowee::Ok;
}

void DuplicateCommand::addArguments(QCommandLineParser &commandLineParser)
{
    commandLineParser.addPositionalArgument("target", "Target File or Directory");
}

Flowee::ReturnCodes DuplicateCommand::preParseArguments(QStringList &positionalArguments)
{
    if (positionalArguments.size() < 2)
        return Flowee::InvalidOptions;
    m_target = positionalArguments.takeLast();
    if (m_target == "." || m_target == "..")
        return Flowee::InvalidOptions;
    return Flowee::Ok;
}
