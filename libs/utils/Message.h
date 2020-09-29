/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2020 Tom Zander <tomz@freedommail.ch>
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
#ifndef MESSAGE_H
#define MESSAGE_H

#include "streaming/ConstBuffer.h"
#include <NetworkEnums.h>
#include <APIProtocol.h>
#include <map>
#include <cassert>

/**
 * A message in a tagged format that we can send or receive using the NetworkManager and NetworkConnection.
 */
class Message
{
public:
    /// default constructor, creates an empty message object
    Message(int serviceId = -1, int messageId = -1);

    bool inline matches(Api::ServiceIds serviceId, int messageId = -1) const {
        return this->serviceId() == serviceId && (messageId == -1 || this->messageId() == messageId);
    }
    bool inline matches(int messageId) const {
        return this->messageId() == messageId;
    }

    /**
     * Create a messge object that is a slice of the \a sharedBuffer.
     * This constructor is the most generic one, it is meant to be used for builders etc. The actual data is a slice of the
     * shared buffer, which is shared between many Message objects.
     *
     * The message can skip the header, in which case it will be autogenerated by the NWM. Set start and bodyStart to the same
     * pointer to indicate there is no header.
     * @param sharedBuffer a refcounted buffer.
     * @param start the pointer into the bigBuffer indicating the start of the data-block.
     * @param bodyStart the beginning of the body of the message.
     * @param end this points to the first byte after the actual data-block that makes up this message. So length = end - start;
     * @see hasHeader()
     */
    explicit Message(const std::shared_ptr<char> &sharedBuffer, const char *start, const char *bodyStart, const char *end);

    /**
     * Create a message object which is based on a shared buffer and a size in that buffer.
     * This constructor is used for cases where the message is at the beginning of the buffer, use the other
     * constructor otherwise.
     * @param sharedBuffer the actual data.
     * @param totalSize the total size of the message data.
     * @param headerSize the total bytes of this message that are header. If zero the basic header will be autogenerated by the NWM.
     * @see hasHeader()
     */
    explicit Message(const std::shared_ptr<char> &sharedBuffer, int totalSize, int headerSize = 0);

    /**
     * Convenience constructor, assumes there is no header, all of the payload is a body.
     */
    Message(const Streaming::ConstBuffer& payload, int serviceId, int messageId = -1);

    /// returns only the header-tags part of the message
    Streaming::ConstBuffer header() const;
    /// returns only the main-message data, without envelope and size-headers
    Streaming::ConstBuffer body() const;
    /// returns the full message-data, including size-leader and envelope-headers
    Streaming::ConstBuffer rawData() const;

    /**
     * Returns true if the message has a header.
     * The Message object is used for both sending messages through the NetworkManager as well as to return messages
     * received from the network layer. A message that comes through a callback is guarenteed to have a header,
     * when application code constructs a message having a header is optional.
     *
     * The NetworkManager will autogenerate a headers before sending it over the wire. It will use the messageId and
     * serviceId you set on the message object.
     */
    bool hasHeader() const;

    /**
     * Add items to the header.
     * Message objects that do not have their own header rely on the network manager
     * to create one for them. Sometimes you just want to add a single item to the header
     * and don't want to bother with creating your own header and in that case you can add
     * an integer here.
     *
     * It is only really useful to add a value to the header, as opposed to the body, of a
     * message if there is a namespacing-conflict between item you want to add and the body,
     * which would lead to unintended sideeffects.
     *
     * @param name the tag-name. Has to be over 9 as the lower ones are reserved.
     */
    inline void setHeaderInt(int name, int value) {
        assert(name >= 10);
        m_headerData.insert(std::make_pair(name, value));
    }
    inline int headerInt(int name, int defaultVal = -1) const {
        auto iter = m_headerData.find(name);
        if (iter == m_headerData.end())
            return defaultVal;
        return iter->second;
    }

    inline void setMessageId(int id) {
        m_headerData[Network::MessageId] = id;
    }
    inline int messageId() const {
        return headerInt(Network::MessageId);
    }

    inline void setServiceId(int id) {
        m_headerData[Network::ServiceId] = id;
    }

    inline int serviceId() const {
        return headerInt(Network::ServiceId);
    }

    inline int size() const {
        return static_cast<int>(m_end - m_start);
    }

    // Return all items to be put in a header.
    const std::map<int, int>& headerData() const {
        return m_headerData;
    }

    /// This int identifies the endpoint / connection the message come in on.
    /// \see NetworkManager::endPoint(int);
    /// \see NetworkConnection::NetworkConnection(NetworkManger*,int);
    int remote;

private:
    std::shared_ptr<char> m_rawData;
    const char *m_start;
    const char *m_bodyStart;
    const char *m_end;

    std::map<int, int> m_headerData;
};

#endif
