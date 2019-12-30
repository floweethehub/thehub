/*
 * This file is part of the Flowee project
 * Copyright (C) 2016, 2019 Tom Zander <tomz@freedommail.ch>
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
#ifndef CONSTBUFFER_H
#define CONSTBUFFER_H

#include <memory>
#include <boost/asio/buffer.hpp>
#include "Logger.h"

namespace Streaming
{

/**
 * Class for holding a reference to an slice of the buffer in buffer_base
 * Since it has a reference to the actual buffer with a shared_array
 * the underlying memory will not be deallocated until all references are removed
 */
class ConstBuffer
{
public:
    static ConstBuffer create(const char *start, size_t size);
    static ConstBuffer create(const std::vector<unsigned char> &vector);

    /// creates an invalid buffer
    ConstBuffer();

    bool isValid() const {
        return m_start != nullptr && m_stop != nullptr;
    }

    bool isEmpty() const {
        return m_start == m_stop;
    }

    /// Construct from already allocated storage.
    /// Keep in mind that a shared_ptr can have a custom dtor if we want to send something special
    explicit ConstBuffer(std::shared_ptr<char> buffer, char const *start, char const *stop);

    char const* begin() const;
    inline char const* cbegin() const {
        return begin();
    }
    char const* end() const;
    inline char const* cend() const {
        return end();
    }

    /// standard indexing operator - assumes begin() + idx < end()
    char operator[](size_t idx) const;

    char const* constData() const;
    /// returns end - begin
    int size() const;

    /// Implement ConvertibleToConstBuffer for asio
    operator boost::asio::const_buffer() const;

    std::shared_ptr<char> internal_buffer() const;

    ConstBuffer mid(int offset, int length = -1) const;

    bool startsWith(const Streaming::ConstBuffer &other) const;

    bool operator==(const Streaming::ConstBuffer &other) const;

private:
    std::shared_ptr<char> m_buffer;
    char const* m_start;
    char const* m_stop;
};
}

inline Log::Item operator<<(Log::Item item, const Streaming::ConstBuffer &buf) {
    if (item.isEnabled()) {
        const bool old = item.useSpace();
        item.nospace() << '{';
        const bool tooLong = buf.size() > 80;
        const uint8_t *end = reinterpret_cast<const uint8_t*>(tooLong ? buf.begin() + 77 : buf.end());
        for (const uint8_t *p = reinterpret_cast<const uint8_t*>(buf.begin()); p < end; ++p) {
            char h = '0' + (*p >> 4);
            if (h > '9') h += 7;
            char l = '0' + (*p & 0xF);
            if (l > '9') l += 7;
            item << h << l;
        }
        if (tooLong) item << "...";
        item << '}';
        if (old)
            return item.space();
    }
    return item;
}
inline Log::SilentItem operator<<(Log::SilentItem item, const Streaming::ConstBuffer&) { return item; }


#endif
