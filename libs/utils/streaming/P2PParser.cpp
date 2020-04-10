/*
 * This file is part of the Flowee project
 * Copyright (C) 2020 Tom Zander <tomz@freedommail.ch>
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
#include "P2PParser.h"

#include <crypto/common.h>

Streaming::ParsingException::ParsingException(const char *message)
    : std::runtime_error(message)
{
}


Streaming::P2PParser::P2PParser(const Message &message)
    : m_constBuffer(message.body()),
      m_privData(m_constBuffer.begin()),
      m_data(m_privData),
      m_end(m_constBuffer.end())
{
}

std::string Streaming::P2PParser::readString()
{
    // first read a varint for size then return the string of that size
    if (m_data > m_end)
        throw ParsingException("Out of range");
    auto size = readCompactInt();
    if (m_data + size > m_end)
        throw ParsingException("Out of range");
    auto ptr = m_data;
    m_data += size;
    return std::string(ptr, size);
}

uint8_t Streaming::P2PParser::readByte()
{
    if (m_data + 1 > m_end)
        throw ParsingException("Out of range");
    return static_cast<uint8_t>(*m_data++);
}

uint16_t Streaming::P2PParser::readWord()
{
    if (m_data + 2 > m_end)
        throw ParsingException("Out of range");

    const unsigned char *ptr = reinterpret_cast<const unsigned char*>(m_data);
    m_data += 2;
    return ReadLE16(ptr);

}

uint16_t Streaming::P2PParser::readWordBE()
{
    if (m_data + 2 > m_end)
        throw ParsingException("Out of range");

    const unsigned char *ptr = reinterpret_cast<const unsigned char*>(m_data);
    m_data += 2;
    return be16toh(*(uint16_t*) ptr);
}

uint32_t Streaming::P2PParser::readInt()
{
    if (m_data + 4 > m_end)
        throw ParsingException("Out of range");

    const unsigned char *ptr = reinterpret_cast<const unsigned char*>(m_data);
    m_data += 4;
    return ReadLE32(ptr);
}

uint64_t Streaming::P2PParser::readLong()
{
    if (m_data + 8 > m_end)
        throw ParsingException("Out of range");

    const unsigned char *ptr = reinterpret_cast<const unsigned char*>(m_data);
    m_data += 8;
    return ReadLE64(ptr);
}

uint64_t Streaming::P2PParser::readCompactInt()
{
    if (m_data > m_end)
        throw ParsingException("Out of range");

    // first convert to unsigned before expanding to 64 bit
    // Otherwise the compiler first goes to signed 64 bit and then to unsigned.
    // And -3 in 64 bit is a lot of leading 1s.
    uint64_t answer = static_cast<uint8_t>(*m_data++);
    switch (answer) {
    case 253:
        return readWord();
    case 254:
        return readInt();
    case 255:
        return readLong();
    default:
        return answer;
    }
}

bool Streaming::P2PParser::readBool()
{
    if (m_data + 1 > m_end)
        throw ParsingException("Out of range");
    return *m_data++ == true;
}

std::vector<char> Streaming::P2PParser::readBytes(int count)
{
    assert(count > 0);
    if (m_data + count > m_end)
        throw ParsingException("Out of range");
    std::vector<char> answer;
    answer.resize(count);
    for (int i = 0; i < count; ++i) {
        answer[i] = m_data[i];
    }
    m_data += count;
    return answer;
}

uint256 Streaming::P2PParser::readUint256()
{
    if (m_data + 32 > m_end)
        throw ParsingException("Out of range");
    uint256 answer(m_data);
    m_data += 32;
    return answer;
}
