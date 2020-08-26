/* This file is part of Flowee
 *
 * Copyright (c) 2017 Aleksei Ermakov
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

#ifndef HTTPENGINE_RANGE_H
#define HTTPENGINE_RANGE_H

#include <QString>

#include "httpengine_export.h"

namespace HttpEngine
{

class HTTPENGINE_EXPORT RangePrivate;

/**
 * @brief HTTP range representation
 *
 * This class provides a representation of HTTP range, described in RFC 7233
 * and used when partial content is requested by the client. When an object is
 * created, optional dataSize can be specified, so that relative ranges can
 * be represented as absolute.
 *
 * @code
 * HttpEngine::Range range(10, -1, 90);
 * range.from();   //  10
 * range.to();     //  89
 * range.length(); //  80
 *
 * range = HttpEngine::Range("-500", 1000);
 * range.from();   // 500
 * range.to();     // 999
 * range.length(); // 500
 *
 * range = HttpEngine::Range(0, -1);
 * range.from();   //   0
 * range.to();     //  -1
 * range.length(); //  -1
 *
 * range = HttpEngine::Range(range, 100);
 * range.from();   //   0
 * range.to();     //  99
 * range.length(); // 100
 * @endcode
 *
 */
class HTTPENGINE_EXPORT Range
{
public:

    /**
     * @brief Create a new range
     *
     * An empty Range is considered invalid.
     */
    Range();

    /**
     * @brief Construct a range from the provided string
     *
     * Parses string representation range and constructs new Range. For raw
     * header "Range: bytes=0-100" only "0-100" should be passed to
     * constructor. dataSize may be supplied so that relative ranges could be
     * represented as absolute values.
     */
    Range(const QString &range, qint64 dataSize = -1);

    /**
     * @brief Construct a range from the provided offsets
     *
     * Initialises a new Range with from and to values. dataSize may be
     * supplied so that relative ranges could be represented as absolute
     * values.
     */
    Range(qint64 from, qint64 to, qint64 dataSize = -1);

    /**
     * @brief Construct a range from the another range's offsets
     *
     * Initialises a new  Range with from and to values of other Range.
     * Supplied dataSize is used instead of other dataSize.
     */
    Range(const Range &other, qint64 dataSize);

    /**
     * @brief Destroy the range
     */
    ~Range();

    /**
     * @brief Assignment operator
     */
    Range& operator=(const Range &other);

    /**
     * @brief Retrieve starting position of range
     *
     * If range is set as 'last N bytes' and dataSize is not set, returns -N.
     */
    qint64 from() const;

    /**
     * @brief Retrieve ending position of range
     *
     * If range is set as 'last N bytes' and dataSize is not set, returns -1.
     * If ending position is not set, and dataSize is not set, returns -1.
     */
    qint64 to() const;

    /**
     * @brief Retrieve length of range
     *
     * If ending position is not set, and dataSize is not set, and range is
     * not set as 'last N bytes', returns -1. If range is invalid, returns -1.
     */
    qint64 length() const;

    /**
     * @brief Retrieve dataSize of range
     *
     * If dataSize is not set, this method returns -1.
     */
    qint64 dataSize() const;

    /**
     * @brief Checks if range is valid
     *
     * Range is considered invalid if it is out of bounds, that is when this
     * inequality is false - (from <= to < dataSize).
     *
     * When HttpRange(const QString&) fails to parse range string, resulting
     * range is also considered invalid.
     *
     * @code
     * HttpEngine::Range range(1, 0, -1);
     * range.isValid(); // false
     *
     * range = HttpEngine::Range(512, 1024);
     * range.isValid(); // true
     *
     * range = HttpEngine::Range("-");
     * range.isValid(); // false
     *
     * range = HttpEngine::Range("abccbf");
     * range.isValid(); // false
     *
     * range = HttpEngine::Range(0, 512, 128);
     * range.isValid(); // false
     *
     * range = HttpEngine::Range(128, 64, 512);
     * range.isValid(); // false
     * @endcode
     */
    bool isValid() const;

    ///
    // @brief Retrieve representation suitable for Content-Range header
    //
    // @code
    // HttpEngine::Range range(0, 100, 1000);
    // range.contentRange(); // "0-100/1000"
    //
    // // When resource size is unknown
    // range = HttpEngine::Range(512, 1024);
    // range.contentRange(); // "512-1024/*"
    //
    // // if range request was bad, return resource size
    // range = HttpEngine::Range(1, 0, 1200);
    // range.contentRange(); // "*\/1200"
    // @endcode
    //
    QString contentRange() const;

private:

    RangePrivate *const d;
};

}

#endif
