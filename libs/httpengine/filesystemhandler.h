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

#ifndef HTTPENGINE_FILESYSTEMHANDLER_H
#define HTTPENGINE_FILESYSTEMHANDLER_H

#include "handler.h"

#include "httpengine_export.h"

namespace HttpEngine
{

class HTTPENGINE_EXPORT FilesystemHandlerPrivate;

/**
 * @brief %Handler for filesystem requests
 *
 * This handler responds to requests for resources on a local filesystem. The
 * constructor is provided with a path to the root directory, which will be
 * used to resolve all paths. The following example creates a handler that
 * serves files from the /var/www directory:
 *
 * @code
 * HttpEngine::FilesystemHandler handler("/var/www");
 * @endcode
 *
 * Requests for resources outside the root will be ignored. The document root
 * can be modified after initialization. It is possible to use a resource
 * directory for the document root.
 */
class HTTPENGINE_EXPORT FilesystemHandler : public Handler
{
    Q_OBJECT

public:

    /**
     * @brief Create a new filesystem handler
     */
    explicit FilesystemHandler(QObject *parent = nullptr);

    /**
     * @brief Create a new filesystem handler from the specified directory
     */
    FilesystemHandler(const QString &documentRoot, QObject *parent = 0);

    /**
     * @brief Set the document root
     *
     * The root path provided is used to resolve each of the requests when
     * they are received.
     */
    void setDocumentRoot(const QString &documentRoot);

protected:

    /**
     * @brief Reimplementation of [Handler::process()](HttpEngine::Handler::process)
     */
    void process(Socket *socket, const QString &path) override;

private:

    FilesystemHandlerPrivate *const d;
    friend class FilesystemHandlerPrivate;
};

}

#endif
