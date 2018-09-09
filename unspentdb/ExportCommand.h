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
#ifndef EXPORTCOMMAND_H
#define EXPORTCOMMAND_H

#include "AbstractCommand.h"
#include <QCommandLineOption>

class ExportCommand : public AbstractCommand
{
public:
    ExportCommand();
    ~ExportCommand();

    QString commandDescription() const;
    Flowee::ReturnCodes run();

protected:
    void addArguments(QCommandLineParser &commandLineParser);

private:
    void write(const Leaf &leaf);

    QCommandLineOption m_filename;
    QTextStream *m_outStream = nullptr;
    QIODevice *m_device = nullptr;
};

#endif
