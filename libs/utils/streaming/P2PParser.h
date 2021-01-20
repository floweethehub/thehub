/*
 * This file is part of the Flowee project
 * Copyright (C) 2020-2021 Tom Zander <tom@flowee.org>
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
#ifndef P2PPARSER_H
#define P2PPARSER_H

#include <Message.h>
#include <uint256.h>


namespace Streaming {

class ParsingException : public std::runtime_error
{
public:
    ParsingException(const char *message);
};

class P2PParser
{
public:
    P2PParser(const Message &message);
    P2PParser(const ConstBuffer &data);

    double readDouble();
    std::string readString();
    uint8_t readByte();
    uint16_t readWord();
    /// read word, but in big-endian
    uint16_t readWordBE();
    uint32_t readInt();
    uint64_t readLong();
    uint64_t readCompactInt();
    bool readBool();
    std::vector<char> readBytes(int32_t count);
    std::vector<uint8_t> readUnsignedBytes(int32_t count);
    uint256 readUint256();

    inline void skip(int32_t bytes) {
        if (m_data + bytes > m_end)
            throw Streaming::ParsingException("Out of range");
        m_data += bytes;
    }

private:
    Streaming::ConstBuffer m_constBuffer;

    const char *m_privData;
    const char *m_data;
    const char *m_end;
};
}

#endif
