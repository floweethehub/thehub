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

#include "TestMiddleware.h"
#include <QTest>

#include <httpengine/handler.h>
#include <httpengine/socket.h>

#include "common/qsimplehttpclient.h"
#include "common/qsocketpair.h"

bool DummyMiddleware::process(HttpEngine::Socket *socket)
{
    socket->writeError(HttpEngine::Socket::Forbidden);
    return false;
}

void TestMiddleware::testProcess()
{
    QSocketPair pair;
    QTRY_VERIFY(pair.isConnected());

    QSimpleHttpClient client(pair.client());
    HttpEngine::Socket *socket = new HttpEngine::Socket(pair.server(), &pair);

    client.sendHeaders("GET", "/");
    QTRY_VERIFY(socket->isHeadersParsed());

    DummyMiddleware middleware;
    HttpEngine::Handler handler;
    handler.addMiddleware(&middleware);
    handler.route(socket, "/");

    QTRY_COMPARE(client.statusCode(), static_cast<int>(HttpEngine::Socket::Forbidden));
}

