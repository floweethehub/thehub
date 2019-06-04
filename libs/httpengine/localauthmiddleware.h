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

#ifndef HTTPENGINE_LOCALAUTHMIDDLEWARE_H
#define HTTPENGINE_LOCALAUTHMIDDLEWARE_H

#include <QVariantMap>

#include "middleware.h"
#include "httpengine_export.h"

namespace HttpEngine
{

class HTTPENGINE_EXPORT LocalAuthMiddlewarePrivate;

/**
 * @brief %Middleware for local file-based authentication
 *
 * This class is intended for authenticating applications running under the
 * same user account as the server. [LocalFile](@ref HttpEngine::LocalFile)
 * is used to expose a token to connecting applications. The client passes the
 * token in a special header and the request is permitted.
 *
 * The file consists of a JSON object in the following format:
 *
 * @code
 * {
 *     "token": "{8a34d0f0-29d0-4e54-b3aa-ce8f8ad65527}"
 * }
 * @endcode
 *
 * Additional data can be added to the object using the setData() method.
 */
class HTTPENGINE_EXPORT LocalAuthMiddleware : public Middleware
{
    Q_OBJECT

public:

    /**
     * @brief Initialize local authentication
     *
     * To determine whether the local file was created successfully, call the
     * exists() method.
     */
    explicit LocalAuthMiddleware(QObject *parent = Q_NULLPTR);

    /**
     * @brief Determine whether the file exists
     */
    bool exists() const;

    /**
     * @brief Retrieve the name of the file used for storing the token
     */
    QString filename() const;

    /**
     * @brief Set additional data to include with the token
     */
    void setData(const QVariantMap &data);

    /**
     * @brief Set the name of the custom header used for confirming the token
     *
     * The default value is "X-Auth-Token".
     */
    void setHeaderName(const QByteArray &name);

    /**
     * @brief Process the request
     *
     * If the token supplied by the client matches, the request is allowed.
     * Otherwise, an HTTP 403 error is returned.
     */
    bool process(Socket *socket) override;

private:

    LocalAuthMiddlewarePrivate *const d;
};

}

#endif
