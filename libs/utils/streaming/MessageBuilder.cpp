/*
 * This file is part of the Flowee project
 * Copyright (C) 2016,2018-2019 Tom Zander <tom@flowee.org>
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
#include "MessageBuilder.h"
#include "MessageBuilder_p.h"
#include "BufferPool.h"

#include <cassert>

int Streaming::Private::serialize(char *data, uint64_t value)
{
    int pos = 0;
    while (true) {
        data[pos] = (value & 0x7F) | (pos ? 0x80 : 0x00);
        if (value <= 0x7F)
            break;
        value = (value >> 7) - 1;
        pos++;
    }

    // reverse
    for (int i = pos / 2; i >= 0; --i) {
        uint8_t tmp = data[i]; // swap
        data[i] = data[pos - i];
        data[pos-i] = tmp;
    }
    return pos + 1;
}

namespace {
    int write(char *data, uint32_t tag, Streaming::ValueType type) {
        // assert(type < 8); // clang complains about this like its impossible to pass in an incorrect into to an enum parameter... Oh, well.
        if (tag >= 31) { // use more than 1 byte
            uint8_t byte = type | 0xF8; // set the 'tag' to all 1s
            data[0] = byte;
            return Streaming::Private::serialize(data +1, tag) + 1;
        }
        else {
            assert(tag < 32);
            uint8_t byte = tag;
            byte = byte << 3;
            byte += type;
            data[0] = byte;
            return 1;
        }
    }
}

Streaming::MessageBuilder::MessageBuilder(MessageType type, int size)
    : m_buffer(new BufferPool(size)),
      m_ownsPool(true),
      m_inHeader(type != NoHeader),
      m_beforeHeader(m_inHeader),
      m_headerSize(0),
      m_messageType(type)
{
}

Streaming::MessageBuilder::MessageBuilder(Streaming::BufferPool &pool, MessageType type)
    : m_buffer(&pool),
      m_ownsPool(false),
      m_inHeader(type != NoHeader),
      m_beforeHeader(m_inHeader),
      m_headerSize(0),
      m_messageType(type)
{
}

Streaming::MessageBuilder::~MessageBuilder()
{
    if (m_ownsPool)
        delete m_buffer;
}

void Streaming::MessageBuilder::add(uint32_t tag, uint64_t value)
{
    if (m_beforeHeader) {
        m_buffer->markUsed(2); // reserve space for the size.
        m_beforeHeader=false;
    }
    int tagSize = write(m_buffer->data(), tag, PositiveNumber);
    m_buffer->markUsed(tagSize);
    tagSize = Private::serialize(m_buffer->data(), value);
    m_buffer->markUsed(tagSize);
}

void Streaming::MessageBuilder::add(uint32_t tag, const std::string &value)
{
    if (m_beforeHeader) {
        m_buffer->markUsed(2); // reserve space for the size.
        m_beforeHeader=false;
    }
    int tagSize = write(m_buffer->data(), tag, String);
    const unsigned int size = value.size();
    tagSize += Private::serialize(m_buffer->data() + tagSize, size);
    m_buffer->markUsed(tagSize);
    memcpy(m_buffer->data(), value.c_str(), value.size());
    m_buffer->markUsed(value.size());
}

void Streaming::MessageBuilder::add(uint32_t tag, const std::vector<char> &data)
{
    addByteArray(tag, data.data(), data.size());
}

void Streaming::MessageBuilder::addByteArray(uint32_t tag, const void *data, int bytes)
{
    if (m_beforeHeader) {
        m_buffer->markUsed(2); // reserve space for the size.
        m_beforeHeader=false;
    }
    int tagSize = write(m_buffer->data(), tag, ByteArray);
    tagSize += Private::serialize(m_buffer->data() + tagSize, bytes);
    m_buffer->markUsed(tagSize);
    memcpy(m_buffer->data(), data, bytes);
    m_buffer->markUsed(bytes);
}

void Streaming::MessageBuilder::add(uint32_t tag, const Streaming::ConstBuffer &data)
{
    if (m_beforeHeader) {
        m_buffer->markUsed(2); // reserve space for the size.
        m_beforeHeader=false;
    }
    int tagSize = write(m_buffer->data(), tag, ByteArray);
    tagSize += Private::serialize(m_buffer->data() + tagSize, data.size());
    m_buffer->markUsed(tagSize);
    memcpy(m_buffer->data(), data.begin(), data.size());
    m_buffer->markUsed(data.size());
}

void Streaming::MessageBuilder::add(uint32_t tag, bool value)
{
    if (m_beforeHeader) {
        m_buffer->markUsed(2); // reserve space for the size.
        m_beforeHeader=false;
    }
    int tagSize = write(m_buffer->data(), tag, value ? BoolTrue : BoolFalse);
    m_buffer->markUsed(tagSize);
    if (m_inHeader && tag == 0) {
        m_inHeader = false;
        m_headerSize = m_buffer->size();
    }
}

void Streaming::MessageBuilder::add(uint32_t tag, int32_t value_)
{
    if (m_beforeHeader) {
        m_buffer->markUsed(2); // reserve space for the size.
        m_beforeHeader=false;
    }
    const ValueType type = value_ >= 0 ? PositiveNumber : NegativeNumber;
    uint64_t value(value_);
    if (value_ < 0)
        value = value_ * -1l;
    assert(value <= 0xFFFFFFFF);
    int tagSize = write(m_buffer->data(), tag, type);
    m_buffer->markUsed(tagSize);
    tagSize = Private::serialize(m_buffer->data(), value);
    m_buffer->markUsed(tagSize);
}

void Streaming::MessageBuilder::add(uint32_t tag, double value)
{
    if (m_beforeHeader) {
        m_buffer->markUsed(2); // reserve space for the size.
        m_beforeHeader=false;
    }
    ValueType type = Double;
    int tagSize = write(m_buffer->data(), tag, type);
    m_buffer->markUsed(tagSize);

    memcpy(m_buffer->data(), &value, 8);
    m_buffer->markUsed(8);
}

void Streaming::MessageBuilder::add(uint32_t tag, const unsigned char *data, unsigned int length)
{
    if (m_beforeHeader) {
        m_buffer->markUsed(2); // reserve space for the size.
        m_beforeHeader=false;
    }
    int tagSize = write(m_buffer->data(), tag, ByteArray);
    tagSize += Private::serialize(m_buffer->data() + tagSize, length);
    m_buffer->markUsed(tagSize);
    memcpy(m_buffer->data(), data, length);
    m_buffer->markUsed(length);
}

void Streaming::MessageBuilder::setMessageSize(int size)
{
    assert(m_messageType != NoHeader);
    assert(m_beforeHeader == false); // should not call this before adding any data.
    if (size > 0x7FFF)
        logCritical(Log::Bitcoin) << "MessageBuilder::setMessageSize Warning. Size too big for 2 bytes:" << size;
    assert(size > 0);
    assert(size <= 0x7FFF);

    uint32_t tmp = static_cast<uint32_t>(size);
    for (int i = 0; i < 2; ++i) {
        m_buffer->begin()[i] = static_cast<char>(tmp & 0xFF);
        tmp = tmp >> 8;
    }
}

Streaming::ConstBuffer Streaming::MessageBuilder::buffer()
{
    assert(m_beforeHeader == false); // should not call this before adding any data.
    if (m_messageType == HeaderAndBody)
        setMessageSize(m_buffer->size());
    Streaming::ConstBuffer answer = m_buffer->commit();
    m_beforeHeader = (m_messageType != NoHeader);
    return answer;
}

Message Streaming::MessageBuilder::message(int serviceId, int messageId, int requestId)
{
    assert(m_beforeHeader == false); // should not call this before adding any data.
    if (m_messageType == HeaderAndBody || m_messageType == HeaderOnly) {
        setMessageSize(m_buffer->size());
        m_beforeHeader = true;
        Message answer(m_buffer->internal_buffer(), m_buffer->begin(), m_buffer->begin() + m_headerSize, m_buffer->end());
        if (serviceId != -1)
            answer.setServiceId(serviceId);
        if (messageId != -1)
            answer.setMessageId(messageId);
        if (requestId != -1)
            answer.setHeaderInt(Api::RequestId, requestId);
        m_buffer->commit();
        return answer;
    }
    m_beforeHeader = (m_messageType != NoHeader);
    if (requestId == -1)
        return Message(m_buffer->commit(), serviceId, messageId);
    auto m = Message(m_buffer->commit(), serviceId, messageId);
    m.setHeaderInt(Api::RequestId, requestId);
    return m;
}

Message Streaming::MessageBuilder::reply(const Message &incoming, int messageId)
{
    assert(m_beforeHeader == false); // should not call this before adding any data.
    assert(m_messageType == NoHeader);
    Message m = message(incoming.serviceId(),
                        messageId == -1 ? incoming.messageId() +1 : messageId);
    const auto headers = incoming.headerData();
    for (auto iter = headers.begin(); iter != headers.end(); ++iter) {
        if (iter->first > 10) // anything below 10 is not allowed to be used by users.
            m.setHeaderInt(iter->first, iter->second);
    }
    return m;
}

int Streaming::serialisedUIntSize(uint64_t unsignedInteger)
{
    char dummy[11];
    return Private::serialize(dummy, unsignedInteger);
}

int Streaming::serialisedIntSize(int32_t signedInteger)
{
    char dummy[11];
    uint64_t data = signedInteger < 0 ? ((int64_t) signedInteger * -1) : signedInteger;
    return Private::serialize(dummy, data);
}
