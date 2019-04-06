/*
 * This file is part of the Flowee project
 * Copyright (C) 2016 Tom Zander <tomz@freedommail.ch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef MESSAGEBUILDER_P_H
#define MESSAGEBUILDER_P_H
/*
 * WARNING USAGE OF THIS HEADER IS RESTRICTED.
 * This Header file is part of the private API and is meant to be used solely by the Streaming component.
 *
 * Usage of this API will likely mean your code will break in interesting ways in the future,
 * or even stop to compile.
 *
 * YOU HAVE BEEN WARNED!!
 */

#include <inttypes.h>

namespace Streaming {
enum ValueType {
    PositiveNumber = 0, // var-int-encoded (between 1 and 9 bytes in length)
    NegativeNumber = 1, // var-int-encoded (between 1 and 9 bytes in length)
    String = 2,         // first a PositiveNumber for the length, then the actual bytes. Never a closing zero. UTF8 encoded.
    ByteArray = 3,      // identical to String, but without encoding.
    BoolTrue = 4,       // not followed with any bytes
    BoolFalse = 5,      // not followed with any bytes
    Double = 6          // followed with 8 bytes
};

namespace Private {
int serialize(char *data, uint64_t value);
bool unserialize(const char *data, int dataSize, int &position, uint64_t &result);
}
}

#endif
