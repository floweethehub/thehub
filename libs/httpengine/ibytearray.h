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

#ifndef HTTPENGINE_IBYTEARRAY_H
#define HTTPENGINE_IBYTEARRAY_H

#include <cctype>

#include <QByteArray>

#include "httpengine_export.h"

namespace HttpEngine
{

/**
 * @brief Case-insensitive subclass of QByteArray
 *
 * The IByteArray is identical to the QByteArray class in all aspects except
 * that it performs comparisons in a case-insensitive manner.
 */
class HTTPENGINE_EXPORT IByteArray : public QByteArray
{
public:

    /// \{
    IByteArray() = default;
    IByteArray(const QByteArray &other) : QByteArray(other) {}
    IByteArray(const IByteArray &other) = default;
    IByteArray(const char *data, int size = -1) : QByteArray(data, size) {}

    inline bool operator==(const QString &s2) const { return toLower() == s2.toLower(); }
    inline bool operator!=(const QString &s2) const { return toLower() != s2.toLower(); }
    inline bool operator<(const QString &s2) const { return toLower() < s2.toLower(); }
    inline bool operator>(const QString &s2) const { return toLower() > s2.toLower(); }
    inline bool operator<=(const QString &s2) const { return toLower() <= s2.toLower(); }
    inline bool operator>=(const QString &s2) const { return toLower() >= s2.toLower(); }

    bool contains(char c) const { return toLower().contains(tolower(c)); }
    bool contains(const char *c) const { return toLower().contains(QByteArray(c).toLower()); }
    bool contains(const QByteArray &a) const { return toLower().contains(a.toLower()); }
    /// \}
};

inline bool operator==(const IByteArray &a1, const char *a2) { return a1.toLower() == QByteArray(a2).toLower(); }
inline bool operator==(const char *a1, const IByteArray &a2) { return QByteArray(a1).toLower() == a2.toLower(); }
inline bool operator==(const IByteArray &a1, const QByteArray &a2) { return a1.toLower() == a2.toLower(); }
inline bool operator==(const QByteArray &a1, const IByteArray &a2) { return a1.toLower() == a2.toLower(); }
inline bool operator==(const IByteArray &a1, const IByteArray &a2) { return a1.toLower() == a2.toLower(); }

inline bool operator!=(const IByteArray &a1, const char *a2) { return a1.toLower() != QByteArray(a2).toLower(); }
inline bool operator!=(const char *a1, const IByteArray &a2) { return QByteArray(a1).toLower() != a2.toLower(); }
inline bool operator!=(const IByteArray &a1, const QByteArray &a2) { return a1.toLower() != a2.toLower(); }
inline bool operator!=(const QByteArray &a1, const IByteArray &a2) { return a1.toLower() != a2.toLower(); }
inline bool operator!=(const IByteArray &a1, const IByteArray &a2) { return a1.toLower() != a2.toLower(); }

inline bool operator<(const IByteArray &a1, const char *a2) { return a1.toLower() < QByteArray(a2).toLower(); }
inline bool operator<(const char *a1, const IByteArray &a2) { return QByteArray(a1).toLower() < a2.toLower(); }
inline bool operator<(const IByteArray &a1, const QByteArray &a2) { return a1.toLower() < a2.toLower(); }
inline bool operator<(const QByteArray &a1, const IByteArray &a2) { return a1.toLower() < a2.toLower(); }
inline bool operator<(const IByteArray &a1, const IByteArray &a2) { return a1.toLower() < a2.toLower(); }

inline bool operator>(const IByteArray &a1, const char *a2) { return a1.toLower() > QByteArray(a2).toLower(); }
inline bool operator>(const char *a1, const IByteArray &a2) { return QByteArray(a1).toLower() > a2.toLower(); }
inline bool operator>(const IByteArray &a1, const QByteArray &a2) { return a1.toLower() > a2.toLower(); }
inline bool operator>(const QByteArray &a1, const IByteArray &a2) { return a1.toLower() > a2.toLower(); }
inline bool operator>(const IByteArray &a1, const IByteArray &a2) { return a1.toLower() > a2.toLower(); }

inline bool operator<=(const IByteArray &a1, const char *a2) { return a1.toLower() <= QByteArray(a2).toLower(); }
inline bool operator<=(const char *a1, const IByteArray &a2) { return QByteArray(a1).toLower() <= a2.toLower(); }
inline bool operator<=(const IByteArray &a1, const QByteArray &a2) { return a1.toLower() <= a2.toLower(); }
inline bool operator<=(const QByteArray &a1, const IByteArray &a2) { return a1.toLower() <= a2.toLower(); }
inline bool operator<=(const IByteArray &a1, const IByteArray &a2) { return a1.toLower() <= a2.toLower(); }

inline bool operator>=(const IByteArray &a1, const char *a2) { return a1.toLower() >= QByteArray(a2).toLower(); }
inline bool operator>=(const char *a1, const IByteArray &a2) { return QByteArray(a1).toLower() >= a2.toLower(); }
inline bool operator>=(const IByteArray &a1, const QByteArray &a2) { return a1.toLower() >= a2.toLower(); }
inline bool operator>=(const QByteArray &a1, const IByteArray &a2) { return a1.toLower() >= a2.toLower(); }
inline bool operator>=(const IByteArray &a1, const IByteArray &a2) { return a1.toLower() >= a2.toLower(); }

}

#endif
