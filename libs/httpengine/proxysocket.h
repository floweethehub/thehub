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

#ifndef HTTPENGINE_PROXYSOCKET_H
#define HTTPENGINE_PROXYSOCKET_H

#include <QHostAddress>
#include <QObject>
#include <QTcpSocket>

#include "socket.h"

/**
 * @brief HTTP socket for connecting to a proxy
 *
 * The proxy socket manages the two socket connections - one for downstream
 * (the client's connection to the server) and one for upstream (the server's
 * connection to the upstream proxy).
 */
class ProxySocket: public QObject
{
    Q_OBJECT

public:

    explicit ProxySocket(HttpEngine::Socket *socket, const QString &path, const QHostAddress &address, quint16 port);

private Q_SLOTS:

    void onDownstreamReadyRead();
    void onDownstreamDisconnected();

    void onUpstreamConnected();
    void onUpstreamReadyRead();
    void onUpstreamError(QTcpSocket::SocketError socketError);

private:

    QString methodToString(HttpEngine::Socket::Method method) const;

    HttpEngine::Socket *mDownstreamSocket;
    QTcpSocket mUpstreamSocket;

    QString mPath;
    bool mHeadersParsed;
    bool mHeadersWritten;

    QByteArray mUpstreamRead;
    QByteArray mUpstreamWrite;
};

#endif
