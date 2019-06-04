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
#include "TestBasicAuthMiddleware.h"

#include <httpengine/handler.h>
#include <httpengine/socket.h>

#include "common/qsimplehttpclient.h"
#include "common/qsocketpair.h"

const QString Username = "username";
const QString Password = "password";

void TestBasicAuthMiddleware::initTestCase()
{
    auth.add(Username, Password);
}

void TestBasicAuthMiddleware::testProcess_data()
{
    QTest::addColumn<bool>("header");
    QTest::addColumn<QString>("username");
    QTest::addColumn<QString>("password");
    QTest::addColumn<int>("status");

    QTest::newRow("no header")
            << false
            << QString()
            << QString()
            << static_cast<int>(HttpEngine::Socket::Unauthorized);

    QTest::newRow("invalid credentials")
            << true
            << Username
            << QString()
            << static_cast<int>(HttpEngine::Socket::Unauthorized);

    QTest::newRow("valid credentials")
            << true
            << Username
            << Password
            << static_cast<int>(HttpEngine::Socket::NotFound);
}

void TestBasicAuthMiddleware::testProcess()
{
    QFETCH(bool, header);
    QFETCH(QString, username);
    QFETCH(QString, password);
    QFETCH(int, status);

    QSocketPair pair;
    QTRY_VERIFY(pair.isConnected());

    QSimpleHttpClient client(pair.client());
    HttpEngine::Socket *socket = new HttpEngine::Socket(pair.server(), &pair);

    HttpEngine::Socket::HeaderMap headers;

    if (header) {
        headers.insert(
            "Authorization",
            "Basic " + QString("%1:%2").arg(username).arg(password).toUtf8().toBase64()
        );
    }

    client.sendHeaders("GET", "/", headers);
    QTRY_VERIFY(socket->isHeadersParsed());

    HttpEngine::Handler handler;
    handler.addMiddleware(&auth);
    handler.route(socket, "/");

    QTRY_COMPARE(client.statusCode(), status);
}
