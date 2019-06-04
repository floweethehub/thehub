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

#include "parser.h"

#include "proxysocket.h"

using namespace HttpEngine;

ProxySocket::ProxySocket(Socket *socket, const QString &path, const QHostAddress &address, quint16 port)
    : QObject(socket),
      mDownstreamSocket(socket),
      mPath(path),
      mHeadersParsed(false),
      mHeadersWritten(false)
{
    connect(mDownstreamSocket, &Socket::readyRead, this, &ProxySocket::onDownstreamReadyRead);
    connect(mDownstreamSocket, &Socket::disconnected, this, &ProxySocket::onDownstreamDisconnected);

    connect(&mUpstreamSocket, &QTcpSocket::connected, this, &ProxySocket::onUpstreamConnected);
    connect(&mUpstreamSocket, &QTcpSocket::readyRead, this, &ProxySocket::onUpstreamReadyRead);
    connect(
        &mUpstreamSocket,
        static_cast<void(QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error),
        this,
        &ProxySocket::onUpstreamError
    );

    mUpstreamSocket.connectToHost(address, port);
}

void ProxySocket::onDownstreamReadyRead()
{
    if (mHeadersWritten) {
        mUpstreamSocket.write(mDownstreamSocket->readAll());
    } else {
        mUpstreamWrite.append(mDownstreamSocket->readAll());
    }
}

void ProxySocket::onDownstreamDisconnected()
{
    mUpstreamSocket.disconnectFromHost();
}

void ProxySocket::onUpstreamConnected()
{
    // Write the status line using the stripped path from the handler
    mUpstreamSocket.write(
        QString("%1 /%2 HTTP/1.1\r\n")
            .arg(methodToString(mDownstreamSocket->method()))
            .arg(mPath)
            .toUtf8()
    );

    // Use the existing headers but insert proxy-related ones
    Socket::HeaderMap headers = mDownstreamSocket->headers();
    QByteArray peerIP = mDownstreamSocket->peerAddress().toString().toUtf8();
    QByteArray origFwd = headers.value("X-Forwarded-For");
    if (origFwd.isNull()) {
        headers.insert("X-Forwarded-For", peerIP);
    } else {
        headers.insert("X-Forwarded-For", origFwd + ", " + peerIP);
    }
    if (!headers.contains("X-Real-IP")) {
        headers.insert("X-Real-IP", peerIP);
    }

    // Write the headers to the socket with the terminating CRLF
    for (auto i = headers.constBegin(); i != headers.constEnd(); ++i) {
        mUpstreamSocket.write(i.key() + ": " + i.value() + "\r\n");
    }
    mUpstreamSocket.write("\r\n");
    mHeadersWritten = true;

    // If there is any data buffered for writing, write it
    if (mUpstreamWrite.size()) {
        mUpstreamSocket.write(mUpstreamWrite);
        mUpstreamWrite.clear();
    }
}

void ProxySocket::onUpstreamReadyRead()
{
    // If the headers have not yet been parsed, then check to see if the end
    // has been reached yet; if they have, just dump data

    if (!mHeadersParsed) {

        // Add to the buffer and check to see if the end was reached
        mUpstreamRead.append(mUpstreamSocket.readAll());
        int index = mUpstreamRead.indexOf("\r\n\r\n");
        if (index != -1) {

            // Parse the headers
            int statusCode;
            QByteArray statusReason;
            Socket::HeaderMap headers;
            if (!Parser::parseResponseHeaders(mUpstreamRead.left(index), statusCode, statusReason, headers)) {
                mDownstreamSocket->writeError(Socket::BadGateway);
                return;
            }

            // Dump the headers back downstream
            mDownstreamSocket->setStatusCode(statusCode, statusReason);
            mDownstreamSocket->setHeaders(headers);
            mDownstreamSocket->writeHeaders();
            mDownstreamSocket->write(mUpstreamRead.mid(index + 4));

            // Remember that headers were parsed and empty the buffer
            mHeadersParsed = true;
            mUpstreamRead.clear();
        }
    } else {
        mDownstreamSocket->write(mUpstreamSocket.readAll());
    }
}

void ProxySocket::onUpstreamError(QAbstractSocket::SocketError socketError)
{
    if (mHeadersParsed) {
        mDownstreamSocket->close();
    } else {
        mDownstreamSocket->writeError(Socket::BadGateway);
    }
}

QString ProxySocket::methodToString(Socket::Method method) const
{
    switch (method) {
    case Socket::OPTIONS: return "OPTIONS";
    case Socket::GET: return "GET";
    case Socket::HEAD: return "HEAD";
    case Socket::POST: return "POST";
    case Socket::PUT: return "PUT";
    case Socket::DELETE: return "DELETE";
    case Socket::TRACE: return "TRACE";
    case Socket::CONNECT: return "CONNECT";
    default: return QString();
    }
}
