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

#include "proxyhandler.h"

#include "proxyhandler_p.h"
#include "proxysocket.h"

using namespace HttpEngine;

ProxyHandlerPrivate::ProxyHandlerPrivate(QObject *parent, const QHostAddress &address, quint16 port)
    : QObject(parent),
      address(address),
      port(port)
{
}

ProxyHandler::ProxyHandler(const QHostAddress &address, quint16 port, QObject *parent)
    : Handler(parent),
      d(new ProxyHandlerPrivate(this, address, port))
{
}

void ProxyHandler::process(Socket *socket, const QString &path)
{
    // Parent the socket to the proxy
    socket->setParent(this);

    // Create a new proxy socket
    new ProxySocket(socket, path, d->address, d->port);
}
