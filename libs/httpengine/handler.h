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

#ifndef HTTPENGINE_HANDLER_H
#define HTTPENGINE_HANDLER_H

#include <QObject>

#include "httpengine_export.h"

class QRegExp;

namespace HttpEngine
{

class Middleware;
class Socket;

class HTTPENGINE_EXPORT HandlerPrivate;

/**
 * @brief Base class for HTTP handlers
 *
 * When a request is received by a [Server](@ref HttpEngine::Server), it
 * invokes the route() method of the root handler which is used to determine
 * what happens to the request. All HTTP handlers derive from this class and
 * should override the protected process() method in order to process the
 * request. Each handler also maintains a list of redirects and sub-handlers
 * which are used in place of invoking process() when one of the patterns
 * match.
 *
 * To add a redirect, use the addRedirect() method. The first parameter is a
 * QRegExp pattern that the request path will be tested against. If it
 * matches, an HTTP 302 redirect will be written to the socket and the request
 * closed. For example, to have the root path "/" redirect to "/index.html":
 *
 * @code
 * HttpEngine::Handler handler;
 * handler.addRedirect(QRegExp("^$"), "/index.html");
 * @endcode
 *
 * To add a sub-handler, use the addSubHandler() method. Again, the first
 * parameter is a QRegExp pattern. If the pattern matches, the portion of the
 * path that matched the pattern is removed from the path and it is passed to
 * the sub-handler's route() method. For example, to have a sub-handler
 * invoked when the path begins with "/api/":
 *
 * @code
 * HttpEngine::Handler handler, subHandler;
 * handler.addSubHandler(QRegExp("^api/"), &subHandler);
 * @endcode
 *
 * If the request doesn't match any redirect or sub-handler patterns, it is
 * passed along to the process() method, which is expected to either process
 * the request or write an error to the socket. The default implementation of
 * process() simply returns an HTTP 404 error.
 */
class HTTPENGINE_EXPORT Handler : public QObject
{
    Q_OBJECT

public:

    /**
     * @brief Base constructor for a handler
     */
    explicit Handler(QObject *parent = nullptr);

    /**
     * @brief Add middleware to the handler
     */
    void addMiddleware(Middleware *middleware);

    /**
     * @brief Add a redirect for a specific pattern
     *
     * The pattern and path will be added to an internal list that will be
     * used when the route() method is invoked to determine whether the
     * request matches any patterns. The order of the list is preserved.
     *
     * The destination path may use "%1", "%2", etc. to refer to captured
     * parts of the pattern. The client will receive an HTTP 302 redirect.
     */
    void addRedirect(const QRegExp &pattern, const QString &path);

    /**
     * @brief Add a handler for a specific pattern
     *
     * The pattern and handler will be added to an internal list that will be
     * used when the route() method is invoked to determine whether the
     * request matches any patterns. The order of the list is preserved.
     */
    void addSubHandler(const QRegExp &pattern, Handler *handler);

    /**
     * @brief Route an incoming request
     */
    void route(Socket *socket, const QString &path);

protected:

    /**
     * @brief Process a request
     *
     * This method should process the request either by fulfilling it, sending
     * a redirect with
     * [Socket::writeRedirect()](@ref HttpEngine::Socket::writeRedirect), or
     * writing an error to the socket using
     * [Socket::writeError()](@ref HttpEngine::Socket::writeError).
     */
    virtual void process(Socket *socket, const QString &path);

private:

    HandlerPrivate *const d;
    friend class HandlerPrivate;
};

}

#endif
