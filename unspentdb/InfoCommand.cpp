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
#include "InfoCommand.h"

#include <streaming/BufferPool.h>
#include <streaming/MessageParser.h>

// private header as we read the db tags directly
#include <utxo/UnspentOutputDatabase_p.h>

#include <QFile>

InfoCommand::InfoCommand()
{
}

QString InfoCommand::commandDescription() const
{
    return "Info\nChecks and prints details about the unspend output DB";
}

Flowee::ReturnCodes InfoCommand::run()
{
    Streaming::BufferPool pool(500);
    foreach (auto df, dbDataFiles()) {
        // TODO convert to info if needed
        QFile file(df.filepath());
        if (!file.open(QIODevice::ReadOnly)) {
            err << "Can't open file " << df.filepath() << endl;
            continue;
        }
        pool.reserve(500);
        qint64 read = file.read(pool.begin(), 500);
        Streaming::MessageParser parser(pool.commit(read));
        Streaming::ParsedType type = parser.next();
        bool done = false;
        while (!done && type == Streaming::FoundTag) {
            switch (static_cast<UODB::MessageTags>(parser.tag())) {
            case UODB::Separator:
                done = true;
                break;
            case UODB::LastBlockId:
                out << "Last Block ID    : " << QString::fromStdString(parser.uint256Data().GetHex()) << endl;
                break;
            case UODB::FirstBlockHeight:
                out << "First Blockheight: " << parser.longData() << endl;
                break;
            case UODB::LastBlockHeight:
                out << "Last Blockheight : " << parser.longData() << endl;
                break;
            case UODB::JumpTableHash:
                out << "Jumptable hash   : " << QString::fromStdString(parser.uint256Data().GetHex()) << endl;
                break;
            case UODB::PositionInFile:
                out << "Filesize         : " << parser.longData() << endl;
                break;

            case UODB::TXID:
            case UODB::OutIndex:
            case UODB::BlockHeight:
            case UODB::OffsetInBlock:
            case UODB::LeafPosition:
            case UODB::LeafPosRelToBucket:
            case UODB::CheapHash:
                err << "Unexpected non-info tag found in info file. " << parser.tag() << endl;
                break;
            default:
                err << "Unknown tag found in info file. " << parser.tag() << endl;
                break;
            }
            type = parser.next();
        }
    }
    return Flowee::Ok;
}
