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

#include <QPair>
#include <QUrl>
#include <QUrlQuery>

#include "parser.h"

using namespace HttpEngine;

void Parser::split(const QByteArray &data, const QByteArray &delim, int maxSplit, QByteArrayList &parts)
{
    int index = 0;

    for (int i = 0; !maxSplit || i < maxSplit; ++i) {
        int nextIndex = data.indexOf(delim, index);

        if (nextIndex == -1) {
            break;
        }

        parts.append(data.mid(index, nextIndex - index));
        index = nextIndex + delim.length();
    }

    // Append whatever remains to the list
    parts.append(data.mid(index));
}

bool Parser::parsePath(const QByteArray &rawPath, QString &path, Socket::QueryStringMap &queryString)
{
    QUrl url(rawPath);
    if (!url.isValid()) {
        return false;
    }

    path = url.path();
    QPair<QString, QString> pair;
    foreach (pair, QUrlQuery(url.query()).queryItems()) {
        queryString.insert(pair.first, pair.second);
    }

    return true;
}

bool Parser::parseHeaderList(const QList<QByteArray> &lines, Socket::HeaderMap &headers)
{
    foreach (const QByteArray &line, lines) {

        QList<QByteArray> parts;
        split(line, ":", 1, parts);

        // Ensure that the delimiter (":") was encountered at least once
        if (parts.count() != 2) {
            return false;
        }

        // Trim excess whitespace and add the header to the list
        headers.insert(parts[0].trimmed(), parts[1].trimmed());
    }

    return true;
}

bool Parser::parseHeaders(const QByteArray &data, QList<QByteArray> &parts, Socket::HeaderMap &headers)
{
    // Split the data into individual lines
    QList<QByteArray> lines;
    split(data, "\r\n", 0, lines);

    // Split the first line into a maximum of three parts
    split(lines.takeFirst(), " ", 2, parts);
    if (parts.count() != 3) {
        return false;
    }

    return parseHeaderList(lines, headers);
}

bool Parser::parseRequestHeaders(const QByteArray &data, Socket::Method &method, QByteArray &path, Socket::HeaderMap &headers)
{
    QList<QByteArray> parts;
    if (!parseHeaders(data, parts, headers)) {
        return false;
    }

    // Only HTTP/1.x versions are supported for now
    if (parts[2] != "HTTP/1.0" && parts[2] != "HTTP/1.1") {
        return false;
    }

    if (parts[0] == "OPTIONS") {
        method = Socket::OPTIONS;
    } else if (parts[0] == "GET") {
        method = Socket::GET;
    } else if (parts[0] == "HEAD") {
        method = Socket::HEAD;
    } else if (parts[0] == "POST") {
        method = Socket::POST;
    } else if (parts[0] == "PUT") {
        method = Socket::PUT;
    } else if (parts[0] == "DELETE") {
        method = Socket::DELETE;
    } else if (parts[0] == "TRACE") {
        method = Socket::TRACE;
    } else if (parts[0] == "CONNECT") {
        method = Socket::CONNECT;
    } else {
        return false;
    }

    path = parts[1];

    return true;
}

bool Parser::parseResponseHeaders(const QByteArray &data, int &statusCode, QByteArray &statusReason, Socket::HeaderMap &headers)
{
    QList<QByteArray> parts;
    if (!parseHeaders(data, parts, headers)) {
        return false;
    }

    statusCode = parts[1].toInt();
    statusReason = parts[2];

    // Ensure a valid status code
    return statusCode >= 100 && statusCode <= 599;
}
