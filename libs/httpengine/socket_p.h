/* This file is part of Flowee
 *
 * Copyright (C) 2017 Nathan Osman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * For the full copy of the License see <http://www.gnu.org/licenses/>
 */

#ifndef HTTPENGINE_SOCKET_P_H
#define HTTPENGINE_SOCKET_P_H

#include "socket.h"

class QTcpSocket;

namespace HttpEngine
{

class SocketPrivate : public QObject
{
    Q_OBJECT

public:

    SocketPrivate(Socket *httpSocket, QTcpSocket *tcpSocket);

    QByteArray statusReason(int statusCode) const;

    QTcpSocket *socket;
    QByteArray readBuffer;

    enum {
        ReadHeaders,
        ReadData,
        ReadFinished
    } readState;

    Socket::Method requestMethod;
    QByteArray requestRawPath;
    QString requestPath;
    Socket::QueryStringMap requestQueryString;
    Socket::HeaderMap requestHeaders;
    qint64 requestDataRead;
    qint64 requestDataTotal;

    enum {
        WriteNone,
        WriteHeaders,
        WriteData,
        WriteFinished
    } writeState;

    int responseStatusCode;
    QByteArray responseStatusReason;
    Socket::HeaderMap responseHeaders;
    qint64 responseHeaderRemaining;

private Q_SLOTS:

    void onReadyRead();
    void onBytesWritten(qint64 bytes);
    void onReadChannelFinished();

private:

    bool readHeaders();
    void readData();

    Socket*const q;
};

}

#endif
