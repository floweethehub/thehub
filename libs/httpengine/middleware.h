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

#ifndef HTTPENGINE_MIDDLEWARE_H
#define HTTPENGINE_MIDDLEWARE_H

#include <QObject>

#include "httpengine_export.h"

namespace HttpEngine
{

class Socket;

/**
 * @brief Pre-handler request processor
 *
 * Middleware sits between the server and the final request handler,
 * determining whether the request should be passed on to the handler.
 */
class HTTPENGINE_EXPORT Middleware : public QObject
{
    Q_OBJECT

public:

    /**
     * @brief Base constructor for middleware
     */
    explicit Middleware(QObject *parent = nullptr);

    /**
     * @brief Determine if request processing should continue
     *
     * This method is invoked when a new request comes in. If true is
     * returned, processing continues. Otherwise, it is assumed that an
     * appropriate error was written to the socket.
     */
    virtual bool process(Socket *socket) = 0;
};

}

#endif
