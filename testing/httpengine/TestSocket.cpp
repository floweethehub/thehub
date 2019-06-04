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

#include "TestSocket.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <httpengine/parser.h>

#include "common/qsimplehttpclient.h"
#include "common/qsocketpair.h"

Q_DECLARE_METATYPE(HttpEngine::Socket::QueryStringMap)

// Utility macro (avoids duplication) that creates a pair of connected
// sockets, a QSimpleHttpClient for the client and a HttpSocket for the
// server
#define CREATE_SOCKET_PAIR() \
    QSocketPair pair; \
    QTRY_VERIFY(pair.isConnected()); \
    QSimpleHttpClient client(pair.client()); \
    HttpEngine::Socket *server = new HttpEngine::Socket(pair.server(), &pair);

const QByteArray Method = "POST";
const QByteArray Path = "/test";
const int StatusCode = 404;
const QByteArray StatusReason = "NOT FOUND";
const QByteArray Data = "test";

TestSocket::TestSocket()
{
    headers.insert("Content-Type", "text/plain");
    headers.insert("Content-Length", QByteArray::number(Data.length()));
}

void TestSocket::testProperties()
{
    CREATE_SOCKET_PAIR();

    client.sendHeaders(Method, Path, headers);

    QTRY_VERIFY(server->isHeadersParsed());
    QCOMPARE(server->method(), HttpEngine::Socket::POST);
    QCOMPARE(server->rawPath(), Path);
    QCOMPARE(server->headers(), headers);

    server->setStatusCode(StatusCode, StatusReason);
    server->setHeaders(headers);
    server->writeHeaders();

    QTRY_COMPARE(client.statusCode(), StatusCode);
    QCOMPARE(client.statusReason(), StatusReason);
    QCOMPARE(client.headers(), headers);
}

void TestSocket::testData()
{
    CREATE_SOCKET_PAIR();

    client.sendHeaders(Method, Path, headers);
    client.sendData(Data);

    QTRY_COMPARE(server->contentLength(), Data.length());
    QTRY_COMPARE(server->bytesAvailable(), Data.length());
    QCOMPARE(server->readAll(), Data);

    server->writeHeaders();
    server->write(Data);

    QTRY_COMPARE(client.data(), Data);
}

void TestSocket::testRedirect()
{
    CREATE_SOCKET_PAIR();

    QSignalSpy disconnectedSpy(pair.client(), SIGNAL(disconnected()));

    server->writeRedirect(Path, true);

    QTRY_COMPARE(client.statusCode(), static_cast<int>(HttpEngine::Socket::MovedPermanently));
    QCOMPARE(client.headers().value("Location"), Path);
    QTRY_COMPARE(disconnectedSpy.count(), 1);
}

void TestSocket::testSignals()
{
    CREATE_SOCKET_PAIR();

    QSignalSpy headersParsedSpy(server, SIGNAL(headersParsed()));
    QSignalSpy readyReadSpy(server, SIGNAL(readyRead()));
    QSignalSpy bytesWrittenSpy(server, SIGNAL(bytesWritten(qint64)));
    QSignalSpy aboutToCloseSpy(server, SIGNAL(aboutToClose()));
    QSignalSpy readChannelFinishedSpy(server, SIGNAL(readChannelFinished()));

    client.sendHeaders(Method, Path, headers);

    QTRY_COMPARE(headersParsedSpy.count(), 1);
    QCOMPARE(readyReadSpy.count(), 0);

    client.sendData(Data);

    QTRY_COMPARE(server->bytesAvailable(), Data.length());
    QVERIFY(readyReadSpy.count() > 0);

    server->writeHeaders();
    server->write(Data);

    QTRY_COMPARE(client.data().length(), Data.length());
    QVERIFY(bytesWrittenSpy.count() > 0);

    qint64 bytesWritten = 0;
    for (int i = 0; i < bytesWrittenSpy.count(); ++i) {
        bytesWritten += bytesWrittenSpy.at(i).at(0).toLongLong();
    }
    QCOMPARE(bytesWritten, Data.length());

    QTRY_COMPARE(aboutToCloseSpy.count(), 0);
    server->close();
    QTRY_COMPARE(aboutToCloseSpy.count(), 1);

    QCOMPARE(readChannelFinishedSpy.count(), 1);
}

void TestSocket::testJson()
{
    CREATE_SOCKET_PAIR();

    QJsonObject object{{"a", "b"}, {"c", 123}};
    QByteArray data = QJsonDocument(object).toJson();

    client.sendHeaders(Method, Path, HttpEngine::Socket::HeaderMap{
        {"Content-Length", QByteArray::number(data.length())},
        {"Content-Type", "application/json"}
    });
    client.sendData(data);

    QTRY_VERIFY(server->isHeadersParsed());
    QTRY_VERIFY(server->bytesAvailable() >= server->contentLength());

    QJsonDocument document;
    QVERIFY(server->readJson(document));
    QCOMPARE(document.object(), object);
}

