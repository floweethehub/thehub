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

#include "TestFilesystemHandler.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include <httpengine/socket.h>
#include <httpengine/filesystemhandler.h>

#include "common/qsimplehttpclient.h"
#include "common/qsocketpair.h"

const QByteArray Data = "test";

void TestFilesystemHandler::initTestCase()
{
    QVERIFY(createFile("outside"));
    QVERIFY(createDirectory("root"));
    QVERIFY(createFile("root/inside"));
}

void TestFilesystemHandler::testRequests_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<int>("statusCode");
    QTest::addColumn<QByteArray>("data");

    QTest::newRow("nonexistent resource")
            << "nonexistent"
            << static_cast<int>(HttpEngine::Socket::NotFound)
            << QByteArray();

    QTest::newRow("outside document root")
            << "../outside"
            << static_cast<int>(HttpEngine::Socket::NotFound)
            << QByteArray();

    QTest::newRow("inside document root")
            << "inside"
            << static_cast<int>(HttpEngine::Socket::OK)
            << Data;

    QTest::newRow("directory listing")
            << ""
            << static_cast<int>(HttpEngine::Socket::OK)
            << QByteArray();
}

void TestFilesystemHandler::testRequests()
{
    QFETCH(QString, path);
    QFETCH(int, statusCode);
    QFETCH(QByteArray, data);

    HttpEngine::FilesystemHandler handler(QDir(dir.path()).absoluteFilePath("root"));

    QSocketPair pair;
    QTRY_VERIFY(pair.isConnected());

    QSimpleHttpClient client(pair.client());
    HttpEngine::Socket *socket = new HttpEngine::Socket(pair.server(), &pair);

    handler.route(socket, path);

    QTRY_COMPARE(client.statusCode(), statusCode);

    if (!data.isNull()) {
        QTRY_COMPARE(client.data(), data);
    }
}

void TestFilesystemHandler::testRangeRequests_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("range");
    QTest::addColumn<int>("statusCode");
    QTest::addColumn<QString>("contentRange");
    QTest::addColumn<QByteArray>("data");

    QTest::newRow("full file")
            << "inside" << ""
            << static_cast<int>(HttpEngine::Socket::OK)
            << ""
            << Data;

    QTest::newRow("range 0-2")
            << "inside" << "0-2"
            << static_cast<int>(HttpEngine::Socket::PartialContent)
            << "bytes 0-2/4"
            << Data.mid(0, 3);

    QTest::newRow("range 1-2")
            << "inside" << "1-2"
            << static_cast<int>(HttpEngine::Socket::PartialContent)
            << "bytes 1-2/4"
            << Data.mid(1, 2);

    QTest::newRow("skip first 1 byte")
            << "inside" << "1-"
            << static_cast<int>(HttpEngine::Socket::PartialContent)
            << "bytes 1-3/4"
            << Data.mid(1);

    QTest::newRow("last 2 bytes")
            << "inside" << "-2"
            << static_cast<int>(HttpEngine::Socket::PartialContent)
            << "bytes 2-3/4"
            << Data.mid(2);

    QTest::newRow("bad range request")
            << "inside" << "abcd"
            << static_cast<int>(HttpEngine::Socket::OK)
            << ""
            << Data;
}

void TestFilesystemHandler::testRangeRequests()
{
    QFETCH(QString, path);
    QFETCH(QString, range);
    QFETCH(int, statusCode);
    QFETCH(QString, contentRange);
    QFETCH(QByteArray, data);

    HttpEngine::FilesystemHandler handler(QDir(dir.path()).absoluteFilePath("root"));

    QSocketPair pair;
    QTRY_VERIFY(pair.isConnected());

    QSimpleHttpClient client(pair.client());
    HttpEngine::Socket *socket = new HttpEngine::Socket(pair.server(), &pair);

    if (!range.isEmpty()) {
        HttpEngine::Socket::HeaderMap inHeaders;
        inHeaders.insert("Range", QByteArray("bytes=") + range.toUtf8());
        client.sendHeaders("GET", path.toUtf8(), inHeaders);
        QTRY_VERIFY(socket->isHeadersParsed());
    }

    handler.route(socket, path);

    QTRY_COMPARE(client.statusCode(), statusCode);

    if (!data.isNull()) {
        QTRY_COMPARE(client.data(), data);
        QCOMPARE(client.headers().value("Content-Length").toInt(), data.length());
        QCOMPARE(client.headers().value("Content-Range"), contentRange.toLatin1());
    }
}

bool TestFilesystemHandler::createFile(const QString &path)
{
    QFile file(QDir(dir.path()).absoluteFilePath(path));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    return file.write(Data) == Data.length();
}

bool TestFilesystemHandler::createDirectory(const QString &path)
{
    return QDir(dir.path()).mkpath(path);
}
