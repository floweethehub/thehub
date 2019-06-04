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

#include "handler.h"
#include "middleware.h"
#include "socket.h"

#include "handler_p.h"

using namespace HttpEngine;

HandlerPrivate::HandlerPrivate(Handler *handler)
    : QObject(handler),
      q(handler)
{
}

Handler::Handler(QObject *parent)
    : QObject(parent),
      d(new HandlerPrivate(this))
{
}

void Handler::addMiddleware(Middleware *middleware)
{
    d->middleware.append(middleware);
}

void Handler::addRedirect(const QRegExp &pattern, const QString &path)
{
    d->redirects.append(Redirect(pattern, path));
}

void Handler::addSubHandler(const QRegExp &pattern, Handler *handler)
{
    d->subHandlers.append(SubHandler(pattern, handler));
}

void Handler::route(Socket *socket, const QString &path)
{
    // Run through each of the middleware
    foreach (Middleware *middleware, d->middleware) {
        if (!middleware->process(socket)) {
            return;
        }
    }

    // Check each of the redirects for a match
    foreach (Redirect redirect, d->redirects) {
        if (redirect.first.indexIn(path) != -1) {
            QString newPath = redirect.second;
            foreach (QString replacement, redirect.first.capturedTexts().mid(1)) {
                newPath = newPath.arg(replacement);
            }
            socket->writeRedirect(newPath.toUtf8());
            return;
        }
    }

    // Check each of the sub-handlers for a match
    foreach (SubHandler subHandler, d->subHandlers) {
        if (subHandler.first.indexIn(path) != -1) {
            subHandler.second->route(socket, path.mid(subHandler.first.matchedLength()));
            return;
        }
    }

    // If no match, invoke the process() method
    process(socket, path);
}

void Handler::process(Socket *socket, const QString &)
{
    // The default response is simply a 404 error
    socket->writeError(Socket::NotFound);
}
