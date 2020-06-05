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

#ifndef HTTPENGINE_LOCALFILE_H
#define HTTPENGINE_LOCALFILE_H

#include <QFile>

#include "httpengine_export.h"

namespace HttpEngine
{

class HTTPENGINE_EXPORT LocalFilePrivate;

/**
 * @brief Locally accessible file
 *
 * LocalFile uses platform-specific functions to create a file containing
 * information that will be accessible only to the local user. This is
 * typically used for storing authentication tokens:
 *
 * @code
 * HttpEngine::LocalFile file;
 * if (file.open()) {
 *     file.write("private data");
 *     file.close();
 * }
 * @endcode
 *
 * By default, the file is stored in the user's home directory and the name of
 * the file is derived from the value of QCoreApplication::applicationName().
 * For example, if the application name was "test" and the user's home
 * directory was `/home/bob`, the absolute path would be `/home/bob/.test`.
 */
class HTTPENGINE_EXPORT LocalFile : public QFile
{
    Q_OBJECT

public:

    /**
     * @brief Create a new local file
     */
    explicit LocalFile(QObject *parent = nullptr);

    /**
     * @brief Attempt to open the file
     *
     * The file must be opened before data can be read or written. This method
     * will return false if the underlying file could not be opened or if this
     * class was unable to set the appropriate file permissions.
     */
    bool openLocalFile();

private:

    LocalFilePrivate *const d;
    friend class LocalFilePrivate;
};

}

#endif
