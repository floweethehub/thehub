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
#ifndef PRUNECOMMAND_H
#define PRUNECOMMAND_H

#include "AbstractCommand.h"

#include <QCommandLineOption>

class QFile;

class PruneCommand : public AbstractCommand
{
public:
    PruneCommand();

    QString commandDescription() const;
    Flowee::ReturnCodes run();

protected:
    void addArguments(QCommandLineParser &commandLineParser);

private:
    bool prune(const std::string &dbFile, const std::string &infoFilename);
    QCommandLineOption m_force;
};

#endif
