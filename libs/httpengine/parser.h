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

#ifndef HTTPENGINE_PARSER_H
#define HTTPENGINE_PARSER_H

#include <QList>

#include "socket.h"

#include "httpengine_export.h"

namespace HttpEngine
{

/**
 * @brief Utility methods for parsing HTTP requests and responses
 *
 * This class provides a set of static methods for parsing HTTP request and
 * response headers. Functionality is broken up into smaller methods in order
 * to make the unit tests simpler.
 */
class HTTPENGINE_EXPORT Parser
{
public:

    /**
     * @brief Split a QByteArray by the provided delimiter
     *
     * If the delimiter is not present in the QByteArray, the resulting list
     * will contain the original QByteArray as its only element. The delimiter
     * must not be empty.
     *
     * If maxSplit is nonzero, the list will contain no more than maxSplit + 1
     * items. If maxSplit is equal to zero, there will be no limit on the
     * number of splits performed.
     */
    static void split(const QByteArray &data, const QByteArray &delim, int maxSplit, QByteArrayList &parts);

    /**
     * @brief Parse and remove the query string from a path
     */
    static bool parsePath(const QByteArray &rawPath, QString &path, Socket::QueryStringMap &queryString);

    /**
     * @brief Parse a list of lines containing HTTP headers
     *
     * Each line is expected to be in the format "name: value". Parsing is
     * immediately aborted if an invalid line is encountered.
     */
    static bool parseHeaderList(const QList<QByteArray> &lines, Socket::HeaderMap &headers);

    /**
     * @brief Parse HTTP headers
     *
     * The specified header data (everything up to the double CRLF) is parsed
     * into a status line and HTTP headers. The parts list will contain the
     * parts from the status line.
     */
    static bool parseHeaders(const QByteArray &data, QList<QByteArray> &parts, Socket::HeaderMap &headers);

    /**
     * @brief Parse HTTP request headers
     */
    static bool parseRequestHeaders(const QByteArray &data, Socket::Method &method, QByteArray &path, Socket::HeaderMap &headers);

    /**
     * @brief Parse HTTP response headers
     */
    static bool parseResponseHeaders(const QByteArray &data, int &statusCode, QByteArray &statusReason, Socket::HeaderMap &headers);
};

}

#endif
