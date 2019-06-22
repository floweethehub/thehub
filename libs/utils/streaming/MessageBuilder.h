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
#ifndef MESSAGEBUILDER_H
#define MESSAGEBUILDER_H

#include "ConstBuffer.h"
#include "Message.h"

#include <uint256.h>

namespace Streaming {

class BufferPool;
class ConstBuffer;

enum MessageType {
    HeaderOnly,
    HeaderAndBody,
    NoHeader
};

int serialisedUIntSize(std::uint64_t unsignedInteger);
int serialisedIntSize(std::int32_t signedInteger);

class MessageBuilder
{
public:
    MessageBuilder(MessageType type = NoHeader, int size = 20000);
    MessageBuilder(BufferPool &pool, MessageType type = NoHeader);
    ~MessageBuilder();

    void add(uint32_t tag, uint64_t value);
    /// add an utf8 string
    void add(uint32_t tag, const std::string &value);
    inline void add(uint32_t tag, const char *utf8String) {
        return add(tag, std::string(utf8String));
    }

    void add(uint32_t tag, const std::vector<char> &data);
    void add(uint32_t tag, const ConstBuffer &data);
    void addByteArray(uint32_t tag, const void *data, int bytes);
    void add(uint32_t tag, bool value);
    void add(uint32_t tag, int32_t value);
    void add(uint32_t tag, double value);
    template<unsigned int BITS>
    void add(uint32_t tag, const base_blob<BITS> &value) {
        addByteArray(tag, static_cast<const void*>(value.begin()), static_cast<int>(value.size()));
    }
    inline void add(uint32_t tag, float value) {
        add(tag, (double) value);
    }

    /**
     * (complete) messages include a message size as the first 4 bytes of the message.
     * So when the MessageBuilder is used to build the header, you need to set the
     * size when the complete message is finished building to update the size at the
     * start of the message.
     * If you use the builder for the type HeaderAndBody, this will happen automatically
     * on a call to buffer() or message().
     * If you use HeaderOnly, you need to call this method.
     * This method will just print a warning if you call it for a message Type of BodyOnly.
     */
    void setMessageSize(int size);

    ConstBuffer buffer();
    /**
     * Create a message based on the build data and the argument header-data.
     */
    Message message(int serviceId = -1, int messageId = -1);

    /**
     * Create a message based on the build data using the
     * incoming message as a base, assuming that the resurned one is a reply to.
     *
     * Please notice that it is only legal to call this method for
     * no-header type messages.
     */
    Message reply(const Message &incoming, int messageId = -1);

private:
    void add(uint32_t tag, const unsigned char *data, unsigned int length);

    BufferPool *m_buffer;
    bool m_ownsPool;
    bool m_inHeader;
    bool m_beforeHeader;
    int m_headerSize;
    MessageType m_messageType;
};

}
#endif
