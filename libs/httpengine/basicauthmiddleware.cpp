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

#include "basicauthmiddleware.h"
#include "ibytearray.h"
#include "parser.h"
#include "socket.h"

#include "basicauthmiddleware_p.h"

using namespace HttpEngine;

BasicAuthMiddlewarePrivate::BasicAuthMiddlewarePrivate(QObject *parent, const QString &realm)
    : QObject(parent),
      realm(realm)
{
}

BasicAuthMiddleware::BasicAuthMiddleware(const QString &realm, QObject *parent)
    : Middleware(parent),
      d(new BasicAuthMiddlewarePrivate(this, realm))
{
}

void BasicAuthMiddleware::add(const QString &username, const QString &password)
{
    d->map.insert(username, password);
}

bool BasicAuthMiddleware::verify(const QString &username, const QString &password)
{
    return d->map.contains(username) && d->map.value(username) == password;
}

bool BasicAuthMiddleware::process(Socket *socket)
{
    // Attempt to extract credentials from the header
    QByteArrayList headerParts = socket->headers().value("Authorization").split(' ');
    if (headerParts.count() == 2 && headerParts.at(0) == IByteArray("Basic")) {

        // Decode the credentials and split into username/password
        QByteArrayList parts;
        Parser::split(
            QByteArray::fromBase64(headerParts.at(1)),
            ":", 1, parts
        );

        // Verify credentials
        if (parts.count() == 2 && verify(parts.at(0), parts.at(1))) {
            return true;
        }
    }

    // Otherwise, inform the client that valid credentials are required
    socket->setHeader("WWW-Authenticate", QString("Basic realm=\"%1\"").arg(d->realm).toUtf8());
    socket->writeError(Socket::Unauthorized);
    return false;
}
