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

#ifndef HTTPENGINE_QSIMPLEHTTPCLIENT_H
#define HTTPENGINE_QSIMPLEHTTPCLIENT_H

#include <QHostAddress>
#include <QObject>
#include <QTcpSocket>

#include <httpengine/parser.h>
#include <httpengine/socket.h>

/**
 * @brief Simple HTTP client for testing purposes
 *
 * This class emulates an extremely simple HTTP client for testing purposes.
 * Once a connection is established, headers and data can be sent and response
 * data is captured for later comparison.
 */
class QSimpleHttpClient : public QObject
{
    Q_OBJECT

public:

    QSimpleHttpClient(QTcpSocket *socket);

    void sendHeaders(const QByteArray &method, const QByteArray &path, const HttpEngine::Socket::HeaderMap &headers = HttpEngine::Socket::HeaderMap());
    void sendData(const QByteArray &data);

    int statusCode() const {
        return mStatusCode;
    }

    QByteArray statusReason() const {
        return mStatusReason;
    }

    HttpEngine::Socket::HeaderMap headers() const {
        return mHeaders;
    }

    QByteArray data() const {
        return mData;
    }

    bool isDataReceived() const;

private Q_SLOTS:

    void onReadyRead();

private:

    QTcpSocket *mSocket;

    QByteArray mBuffer;
    bool mHeadersParsed;

    int mStatusCode;
    QByteArray mStatusReason;
    HttpEngine::Socket::HeaderMap mHeaders;
    QByteArray mData;
};

#endif
