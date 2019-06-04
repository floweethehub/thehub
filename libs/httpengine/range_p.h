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

#ifndef HTTPENGINE_RANGE_P_H
#define HTTPENGINE_RANGE_P_H

#include "range.h"

namespace HttpEngine
{

class RangePrivate
{
public:

    explicit RangePrivate(Range *range);

    qint64 from;
    qint64 to;
    qint64 dataSize;

private:

    Range *const q;
};

}

#endif
