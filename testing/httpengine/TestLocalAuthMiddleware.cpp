/*
 * This file is part of the Flowee project
 * Copyright (c) 2017 Nathan Osman
 * Copyright (C) 2020 Tom Zander <tom@flowee.org>
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

#include "TestLocalAuthMiddleware.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedPointer>
#include <QTest>
#include <QVariantMap>

#include <httpengine/socket.h>
#include <httpengine/localauthmiddleware.h>

#include "common/qsimplehttpclient.h"
#include "common/qsocketpair.h"

const QByteArray HeaderName = "X-Test";
const QByteArray CustomName = "Name";
const char *CustomData = "Data";

void TestLocalAuthMiddleware::testAuth()
{
    QSocketPair pair;
    QTRY_VERIFY(pair.isConnected());

    QSimpleHttpClient client(pair.client());
    HttpEngine::Socket socket(pair.server(), &pair);

    HttpEngine::LocalAuthMiddleware localAuth;
    localAuth.setData(QVariantMap{
        {CustomName, CustomData}
    });
    localAuth.setHeaderName(HeaderName);
    QVERIFY(localAuth.exists());

    QFile file(localAuth.filename());
    QVERIFY(file.open(QIODevice::ReadOnly));

    QVariantMap data = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    QVERIFY(data.contains("token"));
    QCOMPARE(data.value(CustomName).toByteArray(), CustomData);

    client.sendHeaders("GET", "/", HttpEngine::Socket::HeaderMap{
        {HeaderName, data.value("token").toByteArray()}
    });
    QTRY_VERIFY(socket.isHeadersParsed());

    QVERIFY(localAuth.process(&socket));
}

void TestLocalAuthMiddleware::testRemoval()
{
    QScopedPointer<HttpEngine::LocalAuthMiddleware> localAuth(
                new HttpEngine::LocalAuthMiddleware);
    QString filename = localAuth->filename();

    QVERIFY(QFile::exists(filename));
    delete localAuth.take();
    QVERIFY(!QFile::exists(filename));
}

