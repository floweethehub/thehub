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
#ifndef PRUNECOMMAND_H
#define PRUNECOMMAND_H

#include "AbstractCommand.h"

#include <QCommandLineOption>

#include <streaming/BufferPool.h>
#include <streaming/MessageBuilder.h>

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

    struct Bucket {
        uint32_t shorthash;
        std::vector<int> leafPositions; // absolute positions.
    };

    uint32_t copyBucket(const Bucket &bucket, const std::shared_ptr<char> &inputBuf, Streaming::BufferPool &outBuf, Streaming::MessageBuilder &builder);
    QCommandLineOption m_force;
    QCommandLineOption m_backup;
};

#endif
