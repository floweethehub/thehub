/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2017, 2019 Tom Zander <tomz@freedommail.ch>
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
#ifndef MESSAGEPARSER_H
#define MESSAGEPARSER_H

#include "ConstBuffer.h"
#include "../Message.h"

#include <uint256.h>

#include <boost/variant.hpp>
#include <boost/utility/string_ref.hpp>


class Message;

namespace Streaming {

enum ParsedType {
    FoundTag,
    EndOfDocument,
    Error
};

typedef boost::variant<int32_t, bool, uint64_t, std::string, std::vector<char>, double> variant;

/**
 * @brief The MessageParser class is a SOX-like parser of messages.
 * This class allows a zero-copy way to iterate over a message and extract the fields when needed.
 *
 * A simple example usage can be;
 *
 * @code
    MessageParser parser(message.body());
    Streaming::ParsedType type = parser.next();
    while (type == Streaming::FoundTag) {
        if (parser.tag() == SomeEnumValue) {
            logDebug() << parser.data();
        }
        else if (parser.tag() == SomeOtherEnumValue) {
            if (!parser.isString())
                logWarning() << "Bad argument for SomeOtherEnumValue, expected String!";
            else
                logDebug() << "->" << parser.stringData();
        }
        type = parser.next();
    }
    if (type == Streaming::Error) {
        logWarning() << "Failed to parse the message";
    }
    @endcode
 *
 */
class MessageParser
{
public:
    MessageParser(const ConstBuffer &buffer);
    inline MessageParser(const Message &message) : MessageParser(message.body()) {}

    ParsedType next();
    /**
     * @brief peekNext checks for the next tag and returns the tag-id if there is one.
     * Notice that this will not change current!
     * @param success is set to true if there is a next item, otherwise set to false.
     * @return the tag, on success.
     */
    uint32_t peekNext(bool *success = nullptr) const;

    /// returns the tag of the current item.
    uint32_t tag() const;
    /// \internal
    variant data();

    inline bool isInt() const {
        return m_valueState == ValueParsed && m_value.which() == 0;
    }
    inline bool isLong() const {
        return m_valueState == ValueParsed && (m_value.which() == 2 || m_value.which() == 0);
    }
    inline bool isString() const {
        return m_valueState == LazyString || m_value.which() == 3;
    }
    inline bool isBool() const {
        return m_valueState == ValueParsed && m_value.which() == 1;
    }
    inline bool isByteArray() const {
        return m_valueState == LazyByteArray || m_value.which() == 4;
    }
    inline bool isDouble() const {
        return m_valueState == ValueParsed && m_value.which() == 5;
    }

    int32_t intData() const;
    uint64_t longData() const;
    double doubleData() const;
    std::string stringData();
    boost::string_ref rstringData() const;
    bool boolData() const;
    std::vector<char> bytesData() const;
    ConstBuffer bytesDataBuffer() const;
    /// Backwards compatible unsigned char vector.
    std::vector<unsigned char> unsignedBytesData() const;
    /// Return the amount of bytes the string or byte-array is in length
    int dataLength() const;
    uint256 uint256Data() const;

    /// return the amount of bytes consumed up-including the latest parsed tag.
    inline int consumed() const {
        return m_position;
    }

    /// consume a number of bytes without parsing.
    void consume(int bytes);

    static void debugMessage(int section, const Message &message);
    inline static void debugMessage(const Message &message) {
        debugMessage(LOG_DEFAULT_SECTION, message);
    }

    static int32_t read32int(const char *buffer);
    static int16_t read16int(const char *buffer);

private:
    const char *m_privData;
    int m_length;
    int m_position;
    variant m_value;
    uint32_t m_tag;

    enum LazyState {
        ValueParsed,
        LazyByteArray,
        LazyString
    };
    LazyState m_valueState;
    int m_dataStart;
    int m_dataLength;

    ConstBuffer m_constBuffer;
};
}

inline Log::SilentItem operator<<(Log::SilentItem item, const Streaming::variant&) { return item; }
Log::Item operator<<(Log::Item item, const Streaming::variant &data);

#endif
