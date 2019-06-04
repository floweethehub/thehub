/*
 * This file is part of the Flowee project
 * Copyright (c) 2017 Nathan Osman
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

#include "qsocketpair.h"

QSocketPair::QSocketPair()
    : mServerSocket(0)
{
    connect(&mServer, SIGNAL(newConnection()), this, SLOT(onNewConnection()));

    mServer.listen(QHostAddress::LocalHost);
    mClientSocket.connectToHost(mServer.serverAddress(), mServer.serverPort());
}

void QSocketPair::onNewConnection()
{
    mServerSocket = mServer.nextPendingConnection();
}
