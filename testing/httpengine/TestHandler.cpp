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

#include "TestHandler.h"

#include <QRegExp>
#include <QTest>

#include <httpengine/socket.h>

#include "common/qsimplehttpclient.h"
#include "common/qsocketpair.h"

void DummyHandler::process(HttpEngine::Socket *socket, const QString &path)
{
    mPathRemainder = path;
    socket->writeHeaders();
    socket->close();
}

void TestHandler::testRedirect_data()
{
    QTest::addColumn<QRegExp>("pattern");
    QTest::addColumn<QString>("destination");
    QTest::addColumn<QByteArray>("path");
    QTest::addColumn<int>("statusCode");
    QTest::addColumn<QByteArray>("location");

    QTest::newRow("match")
            << QRegExp("\\w+")
            << QString("/two")
            << QByteArray("one")
            << static_cast<int>(HttpEngine::Socket::Found)
            << QByteArray("/two");

    QTest::newRow("no match")
            << QRegExp("\\d+")
            << QString("")
            << QByteArray("test")
            << static_cast<int>(HttpEngine::Socket::NotFound);

    QTest::newRow("captured texts")
            << QRegExp("(\\d+)")
            << QString("/path/%1")
            << QByteArray("123")
            << static_cast<int>(HttpEngine::Socket::Found)
            << QByteArray("/path/123");
}

void TestHandler::testRedirect()
{
    QFETCH(QRegExp, pattern);
    QFETCH(QString, destination);
    QFETCH(QByteArray, path);
    QFETCH(int, statusCode);

    QSocketPair pair;
    QTRY_VERIFY(pair.isConnected());

    QSimpleHttpClient client(pair.client());
    HttpEngine::Socket *socket = new HttpEngine::Socket(pair.server(), &pair);

    client.sendHeaders("GET", path);
    QTRY_VERIFY(socket->isHeadersParsed());

    HttpEngine::Handler handler;
    handler.addRedirect(pattern, destination);
    handler.route(socket, socket->path());

    QTRY_COMPARE(client.statusCode(), statusCode);

    if (statusCode == HttpEngine::Socket::Found) {
        QFETCH(QByteArray, location);
        QCOMPARE(client.headers().value("Location"), location);
    }
}

void TestHandler::testSubHandler_data()
{
    QTest::addColumn<QRegExp>("pattern");
    QTest::addColumn<QByteArray>("path");
    QTest::addColumn<QString>("pathRemainder");
    QTest::addColumn<int>("statusCode");

    QTest::newRow("match")
            << QRegExp("\\w+")
            << QByteArray("test")
            << QString("")
            << static_cast<int>(HttpEngine::Socket::OK);

    QTest::newRow("no match")
            << QRegExp("\\d+")
            << QByteArray("test")
            << QString("")
            << static_cast<int>(HttpEngine::Socket::NotFound);

    QTest::newRow("path")
            << QRegExp("one/")
            << QByteArray("one/two")
            << QString("two")
            << static_cast<int>(HttpEngine::Socket::OK);
}

void TestHandler::testSubHandler()
{
    QFETCH(QRegExp, pattern);
    QFETCH(QByteArray, path);
    QFETCH(QString, pathRemainder);
    QFETCH(int, statusCode);

    QSocketPair pair;
    QTRY_VERIFY(pair.isConnected());

    QSimpleHttpClient client(pair.client());
    HttpEngine::Socket *socket = new HttpEngine::Socket(pair.server(), &pair);

    client.sendHeaders("GET", path);
    QTRY_VERIFY(socket->isHeadersParsed());

    DummyHandler subHandler;
    HttpEngine::Handler handler;
    handler.addSubHandler(pattern, &subHandler);

    handler.route(socket, socket->path());

    QTRY_COMPARE(client.statusCode(), statusCode);
    QCOMPARE(subHandler.mPathRemainder, pathRemainder);
}

