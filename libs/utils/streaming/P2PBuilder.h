/*
 * This file is part of the Flowee project
 * Copyright (C) 2016,2018-2020 Tom Zander <tom@flowee.org>
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
#ifndef P2PBUILDER_H
#define P2PBUILDER_H

#include "ConstBuffer.h"
#include <Message.h>

#include <uint256.h>

namespace Streaming {

enum LengthIndicator {
    RawBytes,
    WithLength
};

class BufferPool;

class P2PBuilder
{
public:
    P2PBuilder(BufferPool &pool);
    /// add an utf8 string
    void writeString(const std::string &value, LengthIndicator length);
    inline void writeString(const char *utf8String, LengthIndicator length) {
        return writeString(std::string(utf8String), length);
    }

    void writeByteArray(const ConstBuffer &data, LengthIndicator length);
    void writeByteArray(const void *data, int bytes, LengthIndicator length);
    inline void writeByteArray(const std::vector<int8_t> &data, LengthIndicator length) {
        writeByteArray(data.data(), static_cast<int>(data.size()), length);
    }
    inline void writeByteArray(const std::vector<uint8_t> &data, LengthIndicator length) {
        writeByteArray(reinterpret_cast<const void*>(data.data()), static_cast<int>(data.size()), length);
    }
    void writeLong(uint64_t value);
    void writeBool(bool value);
    void writeInt(int32_t value);
    void writeFloat(double value);
    template<uint32_t BITS>
    void writeByteArray(const base_blob<BITS> &value, LengthIndicator length) {
        writeByteArray(static_cast<const void*>(value.begin()), static_cast<int32_t>(value.size()), length);
    }
    inline void writeFloat(float value) {
        writeFloat((double) value);
    }
    void writeCompactSize(uint64_t value);
    void writeByte(uint8_t value);
    void writeWord(uint16_t value);

    ConstBuffer buffer();

    /**
     * Create a message based on the build data and the argument header-data.
     */
    Message message(int messageId = -1);

private:
    BufferPool *m_buffer;
};

}
#endif
