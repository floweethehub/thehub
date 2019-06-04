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

#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include "localauthmiddleware.h"
#include "socket.h"

#include "localauthmiddleware_p.h"

using namespace HttpEngine;

LocalAuthMiddlewarePrivate::LocalAuthMiddlewarePrivate(QObject *parent)
    : QObject(parent),
      tokenHeader("X-Auth-Token"),
      token(QUuid::createUuid().toString())
{
    updateFile();
}

LocalAuthMiddlewarePrivate::~LocalAuthMiddlewarePrivate()
{
    file.remove();
}

void LocalAuthMiddlewarePrivate::updateFile()
{
    if (file.open()) {
        file.write(QJsonDocument(QJsonObject::fromVariantMap(data)).toJson());
        file.close();
    }
}

LocalAuthMiddleware::LocalAuthMiddleware(QObject *parent)
    : Middleware(parent),
      d(new LocalAuthMiddlewarePrivate(this))
{
}

bool LocalAuthMiddleware::exists() const
{
    return d->file.exists();
}

QString LocalAuthMiddleware::filename() const
{
    return d->file.fileName();
}

void LocalAuthMiddleware::setData(const QVariantMap &data)
{
    d->data = data;
    d->data.insert("token", d->token);
    d->updateFile();
}

void LocalAuthMiddleware::setHeaderName(const QByteArray &name)
{
    d->tokenHeader = name;
}

bool LocalAuthMiddleware::process(Socket *socket)
{
    if (socket->headers().value(d->tokenHeader) != d->token) {
        socket->writeError(Socket::Forbidden);
        return false;
    }

    return true;
}
