/*
 * This file is part of the Flowee project
 * Copyright (C) 2016,2018-2019 Tom Zander <tomz@freedommail.ch>
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
#include "BufferPool.h"
#include "../utilstrencodings.h"

#include <boost/math/constants/constants.hpp>

Streaming::BufferPool::BufferPool(int default_size)
    : m_buffer(std::shared_ptr<char>(new char[default_size], std::default_delete<char[]>())),
    m_readPointer(m_buffer.get()),
    m_writePointer(m_buffer.get()),
    m_defaultSize(default_size),
    m_size(default_size)
{
}

Streaming::BufferPool::BufferPool(BufferPool&& other)
    : m_buffer(std::move(other.m_buffer)),
    m_readPointer(std::move(other.m_readPointer)),
    m_writePointer(std::move(other.m_writePointer)),
    m_defaultSize(std::move(other.m_defaultSize)),
    m_size(std::move(other.m_size))
{
}

Streaming::BufferPool::BufferPool(std::shared_ptr<char> &data, int length, bool staticBuf)
    : m_buffer(data),
    m_readPointer(&(*data)),
    m_writePointer(m_readPointer),
    m_defaultSize(staticBuf ? -1 : length),
    m_size(length)
{
}

Streaming::BufferPool& Streaming::BufferPool::operator=(BufferPool&& rhs)
{
    m_buffer = std::move(rhs.m_buffer);
    m_readPointer = std::move(rhs.m_readPointer);
    m_writePointer = std::move(rhs.m_writePointer);
    m_defaultSize = std::move(rhs.m_defaultSize);
    m_size = std::move(rhs.m_size);
    return *this;
}

int Streaming::BufferPool::capacity() const
{
    assert(m_writePointer <= m_buffer.get() + m_size);
    auto distance = std::distance<char const*>(m_writePointer, m_buffer.get() + m_size);
    assert(distance < 0xeFFFFFFF);
    return static_cast<int>(distance);
}

void Streaming::BufferPool::forget(int rc)
{
    m_readPointer += rc;
    assert(m_readPointer <= m_buffer.get() + m_size);
}

Streaming::ConstBuffer Streaming::BufferPool::commit(int usedBytes)
{
    assert(usedBytes >= 0);
    m_writePointer += usedBytes;
    assert(m_writePointer <= m_buffer.get() + m_size);
    assert(m_writePointer >= m_readPointer);

    const char * begin = m_readPointer;
    m_readPointer = m_writePointer;
    assert(m_readPointer <= m_buffer.get() + m_size); // Or just less than?
    const char * end = m_writePointer;
    return ConstBuffer(m_buffer, begin, end);
}

int Streaming::BufferPool::size() const
{
    return static_cast<int>(end() - begin());
}

void Streaming::BufferPool::clear()
{
    m_readPointer = nullptr;
    m_writePointer = nullptr;
    m_buffer.reset();
    m_size = m_defaultSize;
}

void Streaming::BufferPool::writeInt32(unsigned int data)
{
    unsigned int d = data;
    m_writePointer[0] = static_cast<char>(d & 0xFF);
    d = d >> 8;
    m_writePointer[1] = static_cast<char>(d & 0xFF);
    d = d >> 8;
    m_writePointer[2] = static_cast<char>(d & 0xFF);
    d = d >> 8;
    m_writePointer[3] = static_cast<char>(d & 0xFF);
    markUsed(4);
}

void Streaming::BufferPool::writeHex(const char *string)
{
    if (string[0] == '0' && string[1] == 'x')
        string += 2;
    while (m_writePointer < m_buffer.get() + m_size) {
        while (isspace(*string))
            string++;
        int8_t c = HexDigit(*string++);
        if (c == (int8_t) -1)
            break;
        uint8_t n = (c << 4);
        c = HexDigit(*string++);
        if (c == (int8_t)-1)
            break;
        m_writePointer[0] = n | c;
        m_writePointer += 1;
    }
}

int Streaming::BufferPool::offset() const
{
    if (m_buffer.get() == nullptr)
        return 0;
    return static_cast<int>(m_writePointer - m_buffer.get());
}

Streaming::ConstBuffer Streaming::BufferPool::createBufferSlice(char const* start, char const* stop) const
{
    assert(stop >= start);
    assert(start >= begin());
    assert(start <= end());
    assert(stop >= begin());
    assert(stop <= end());
    return ConstBuffer(m_buffer, start, stop);
}

void Streaming::BufferPool::change_capacity(int bytes)
{
    if (m_defaultSize == -1)
        throw std::runtime_error("Out of buffer memory");
    std::int64_t unprocessed = m_writePointer - m_readPointer;
    assert(unprocessed >= 0);
    if (unprocessed + bytes <= m_defaultSize) { // unprocessed > buffer_size
        m_size = m_defaultSize;
    }
    else if (unprocessed + bytes > m_size) { // would not fit in 'size'
        assert(unprocessed < 0x8FFFFFFF); // fits in signed int, aka 2GiB.
        std::int64_t newSize = std::max(bytes + unprocessed, static_cast<std::int64_t>(m_size) * 2);
        newSize = std::min<std::int64_t>(0x8FFFFFFF, newSize); // fits in signed int.
        m_size = static_cast<int>(newSize);
        assert(m_size >= 0);
        assert(m_size >= m_defaultSize);
    }
    std::shared_ptr<char> newBuffer = std::shared_ptr<char>(new char[m_size], std::default_delete<char[]>());

    std::memcpy(newBuffer.get(), m_readPointer, static_cast<size_t>(unprocessed)); // Read pointer still points to the old buffer
    m_buffer = newBuffer;
    m_readPointer = m_buffer.get();
    m_writePointer = m_readPointer + unprocessed;
}

void Streaming::BufferPool::reserve(int bytes)
{
    assert(bytes >= 0);
    if (m_readPointer == nullptr) {
        m_buffer = std::shared_ptr<char>(new char[m_size], std::default_delete<char[]>());
        m_readPointer = m_buffer.get();
        m_writePointer = m_readPointer;
    }
    if (capacity() < bytes)
        change_capacity(bytes);
}

std::shared_ptr<char> Streaming::BufferPool::internal_buffer() const
{
    return m_buffer;
}

char Streaming::BufferPool::operator[](size_t idx) const
{
    assert(begin() + idx < end());
    return *(begin() + idx);
}
