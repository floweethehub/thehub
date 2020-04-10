/*
 * This file is part of the Flowee project
 * Copyright (C) 2016,2018-2020 Tom Zander <tomz@freedommail.ch>
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
#include "P2PBuilder.h"
#include "BufferPool.h"
#include <crypto/common.h>

#include <cassert>

namespace {
    int writeCompactSize_priv(char *data, uint64_t value)
    {
        int size = 1;
        if (value < 253) {
            // nothing to do
        }
        else if (value <= std::numeric_limits<unsigned short>::max()) {
            uint16_t copy = static_cast<uint16_t>(value);
            memcpy(data + 1, &copy, 2);
            size += 2;
            value = 253;
        }
        else if (value <= std::numeric_limits<unsigned int>::max()) {
            uint32_t copy = static_cast<uint32_t>(value);
            memcpy(data + 1, &copy, 4);
            size += 4;
            value = 254;
        }
        else {
            memcpy(data + 1, &value, 8);
            size += 8;
            value = 255;
        }
        *data = static_cast<uint8_t>(value);
        return size;
    }
}

Streaming::P2PBuilder::P2PBuilder(Streaming::BufferPool &pool)
    : m_buffer(&pool)
{
}

void Streaming::P2PBuilder::writeLong(uint64_t value)
{
    WriteLE64(reinterpret_cast<uint8_t*>(m_buffer->data()), value);
    m_buffer->markUsed(8);
}

void Streaming::P2PBuilder::writeString(const std::string &value, LengthIndicator length)
{
    if (length == WithLength) {
        int tagSize = writeCompactSize_priv(m_buffer->data(), value.size());
        m_buffer->markUsed(tagSize);
    }
    memcpy(m_buffer->data(), value.c_str(), value.size());
    m_buffer->markUsed(value.size());
}

void Streaming::P2PBuilder::writeByteArray( const void *data, int bytes, LengthIndicator length)
{
    if (length == WithLength) {
        int tagSize = writeCompactSize_priv(m_buffer->data(), bytes);
        m_buffer->markUsed(tagSize);
    }
    memcpy(m_buffer->data(), data, bytes);
    m_buffer->markUsed(bytes);
}

void Streaming::P2PBuilder::writeByteArray(const Streaming::ConstBuffer &data, LengthIndicator length)
{
    if (length == WithLength) {
        int tagSize = writeCompactSize_priv(m_buffer->data(), data.size());
        m_buffer->markUsed(tagSize);
    }
    memcpy(m_buffer->data(), data.begin(), data.size());
    m_buffer->markUsed(data.size());
}

void Streaming::P2PBuilder::writeBool(bool value)
{
    char f = value;
    m_buffer->data()[0] = f;
    m_buffer->markUsed(1);
}

void Streaming::P2PBuilder::writeInt(int32_t value)
{
    WriteLE32(reinterpret_cast<uint8_t*>(m_buffer->data()), value);
    m_buffer->markUsed(4);
}

void Streaming::P2PBuilder::writeFloat(double value)
{
    memcpy(m_buffer->data(), &value, 8);
    m_buffer->markUsed(8);
}

void Streaming::P2PBuilder::writeCompactSize(uint64_t value)
{
    int tagSize = writeCompactSize_priv(m_buffer->data(), value);
    m_buffer->markUsed(tagSize);
}

void Streaming::P2PBuilder::writeByte(uint8_t value)
{
    *m_buffer->data() = value;
    m_buffer->markUsed(1);
}

void Streaming::P2PBuilder::writeWord(uint16_t value)
{
    WriteLE16(reinterpret_cast<uint8_t*>(m_buffer->data()), value);
    m_buffer->markUsed(2);
}

Streaming::ConstBuffer Streaming::P2PBuilder::buffer()
{
    return m_buffer->commit();
}

Message Streaming::P2PBuilder::message(int messageId)
{
    Message answer(m_buffer->internal_buffer(), m_buffer->begin(), m_buffer->begin(), m_buffer->end());
    answer.setServiceId(Api::LegacyP2P);
    if (messageId != -1)
        answer.setMessageId(messageId);
    m_buffer->commit();
    return answer;
}
