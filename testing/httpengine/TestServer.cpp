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

#include "TestServer.h"
#include <QTcpSocket>
#include <QTest>

#if !defined(QT_NO_SSL)
#  include <QFile>
#  include <QSslCertificate>
#  include <QSslConfiguration>
#  include <QSslKey>
#  include <QSslSocket>
#endif

#include <httpengine/handler.h>

#include "common/qsimplehttpclient.h"

void TestHandler_Server::process(HttpEngine::WebRequest *request)
{
    mPath = request->path();
    request->deleteLater();
}

void TestServer::testServer()
{
    TestHandler_Server handler;
    HttpEngine::Server server(std::bind(&TestHandler_Server::process, &handler, std::placeholders::_1));

    QVERIFY(server.listen(QHostAddress::LocalHost));

    QTcpSocket socket;
    socket.connectToHost(server.serverAddress(), server.serverPort());
    QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);

    QSimpleHttpClient client(&socket);
    client.sendHeaders("GET", "/test");

    QTRY_COMPARE(handler.mPath, QString("/test"));
}

#if !defined(QT_NO_SSL)
void TestServer::testSsl()
{
    QFile keyFile(":/key.pem");
    QVERIFY(keyFile.open(QIODevice::ReadOnly));

    QSslKey key(&keyFile, QSsl::Rsa);
    QList<QSslCertificate> certs = QSslCertificate::fromPath(":/cert.pem");

    QSslConfiguration config;
    config.setPrivateKey(key);
    config.setLocalCertificateChain(certs);

    HttpEngine::Server server;
    server.setSslConfiguration(config);

    QVERIFY(server.listen(QHostAddress::LocalHost));

    QSslSocket socket;
    socket.setCaCertificates(certs);
    socket.connectToHost(server.serverAddress(), server.serverPort());
    socket.setPeerVerifyName("localhost");

    QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);

    socket.startClientEncryption();
    QTRY_VERIFY(socket.isEncrypted());
}
#endif

