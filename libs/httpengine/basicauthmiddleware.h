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

#ifndef HTTPENGINE_BASICAUTHMIDDLEWARE_H
#define HTTPENGINE_BASICAUTHMIDDLEWARE_H

#include "middleware.h"

#include "httpengine_export.h"

namespace HttpEngine
{

class HTTPENGINE_EXPORT BasicAuthMiddlewarePrivate;

/**
 * @brief %Middleware for HTTP basic authentication
 *
 * HTTP Basic authentication allows access to specific resources to be
 * restricted. This class uses a map to store accepted username/password
 * combinations, which are then used for authenticating requests. To use a
 * different method of authentication, override the verify() method in a
 * derived class.
 */
class HTTPENGINE_EXPORT BasicAuthMiddleware : public Middleware
{
    Q_OBJECT

public:

    /**
     * @brief Base constructor for the middleware
     *
     * The realm string is shown to a client when credentials are requested.
     */
    BasicAuthMiddleware(const QString &realm, QObject *parent = Q_NULLPTR);

    /**
     * @brief Add credentials to the list
     *
     * If the username has already been added, its password will be replaced
     * with the new one provided.
     */
    void add(const QString &username, const QString &password);

    /**
     * @brief Process the request
     *
     * If the verify() method returns true, the client will be granted access
     * to the resources. Otherwise, 401 Unauthorized will be returned.
     */
    bool process(Socket *socket) override;

protected:

    /**
     * @brief Determine if the client is authorized
     */
    virtual bool verify(const QString &username, const QString &password);

private:

    BasicAuthMiddlewarePrivate *const d;
};

}

#endif
