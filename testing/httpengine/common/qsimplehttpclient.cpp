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

#include "qsimplehttpclient.h"

QSimpleHttpClient::QSimpleHttpClient(QTcpSocket *socket)
    : mSocket(socket),
      mHeadersParsed(false),
      mStatusCode(0)
{
    connect(mSocket, SIGNAL(readyRead()), this, SLOT(onReadyRead()));

    // Immediately trigger a read
    onReadyRead();
}

void QSimpleHttpClient::sendHeaders(const QByteArray &method, const QByteArray &path, const HttpEngine::Socket::HeaderMap &headers)
{
    QByteArray data = method + " " + path + " HTTP/1.0\r\n";
    for (auto i = headers.constBegin(); i != headers.constEnd(); ++i) {
        data.append(i.key() + ": " + i.value() + "\r\n");
    }
    data.append("\r\n");

    mSocket->write(data);
}

void QSimpleHttpClient::sendData(const QByteArray &data)
{
    mSocket->write(data);
}

void QSimpleHttpClient::onReadyRead()
{
    if (mHeadersParsed) {
        mData.append(mSocket->readAll());
    } else {
        mBuffer.append(mSocket->readAll());

        // Parse the headers if the double CRLF sequence was found
        int index = mBuffer.indexOf("\r\n\r\n");
        if (index != -1) {
            HttpEngine::Parser::parseResponseHeaders(mBuffer.left(index), mStatusCode, mStatusReason, mHeaders);

            mHeadersParsed = true;
            mData.append(mBuffer.mid(index + 4));
        }
    }
}

bool QSimpleHttpClient::isDataReceived() const
{
    return mHeaders.contains("Content-Length") &&
            mData.length() >= mHeaders.value("Content-Length").toInt();
}
