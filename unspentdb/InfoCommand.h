/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tom@flowee.org>
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
#ifndef INFOCOMMAND_H
#define INFOCOMMAND_H

#include "AbstractCommand.h"

#include <QCommandLineOption>

class InfoCommand : public AbstractCommand
{
public:
    InfoCommand();

    QString commandDescription() const;
    Flowee::ReturnCodes run();

protected:
    void addArguments(QCommandLineParser &commandLineParser);

private:
    uint256 printBucketUsage(int startPos, QFile *infoFile);

    void printStats(uint32_t *tables, const DatabaseFile &df);


    QCommandLineOption m_printUsage;
};

#endif
