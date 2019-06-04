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

#ifndef HTTPENGINE_PROXYHANDLER_H
#define HTTPENGINE_PROXYHANDLER_H

#include <QHostAddress>

#include "handler.h"

#include "httpengine_export.h"

namespace HttpEngine
{

class HTTPENGINE_EXPORT ProxyHandlerPrivate;

/**
 * @brief %Handler that routes HTTP requests to an upstream server
 */
class HTTPENGINE_EXPORT ProxyHandler : public Handler
{
    Q_OBJECT

public:

    /**
     * @brief Create a new proxy handler
     */
    ProxyHandler(const QHostAddress &address, quint16 port, QObject *parent = nullptr);

protected:

    /**
     * @brief Reimplementation of [Handler::process()](HttpEngine::Handler::process)
     */
    void process(Socket *socket, const QString &path) override;

private:

    ProxyHandlerPrivate *const d;
};

}

#endif
