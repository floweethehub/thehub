/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2017 Tom Zander <tomz@freedommail.ch>
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

#ifndef APIRPCBINDING_H
#define APIRPCBINDING_H

#include <string>
#include <functional>

namespace Streaming {
    class MessageBuilder;
}
class UniValue;
class Message;

namespace Api
{
    /**
     * This class, and its subclasses RpcParser / DirectParser are the baseclasses for specific commands.
     *
     * In the API we have specific incoming messages which map to a Parser implementation.
     * When a new request comes in from the network, the specific parser that can handle this
     * is instantiated and then based on the api server finding either a RpcParser or a DirectParser
     * its virtual methods will be called.
     */
    class Parser {
    public:
        enum ParserType {
            WrapsRPCCall,
            IncludesHandler
        };

        Parser(ParserType type, int replyMessageId, int messageSize = -1);
        virtual ~Parser() {}

        inline ParserType type() const {
            return m_type;
        }

        /// Returns the message-id we set in the answer message. Typically an enum from the APIProtocol.h
        inline int replyMessageId() const {
            return m_replyMessageId;
        }

    protected:
        int m_messageSize;
        int m_replyMessageId;
        ParserType m_type;
    };

    /**
     * A parser that binds to an existing (legacy) RPC call to execute the API call.
     * When a new request comes in from the network, the specific parser that can handle this
     * is instantiated and then createReqeust() is called with the message we received from the network,
     * followed by a call to buildReply() which is meant to build the answer.
     */
    class RpcParser : public Parser {
    public:
        /**
         * Constructor, meant for overloading. Subclasses should pass in the parameters.
         * @param method  this parameter is the name of the RPC method we map to.
         * @param replyMessageId the enum value of the answer message that the network client expects.
         * @param messageSize when passed in, this will be the hardcoded size and calculateMessageSize() will not be called.
         */
        RpcParser(const std::string &method, int replyMessageId, int messageSize = -1);

        /**
         * @brief messageSize returns the amount of bytes we should reserve for the reply.
         * @param result is the result we received from the RPC layer.
         * @return either the messageSize passed into the constructor, or the output of calculateMessageSize()
         */
        inline int messageSize(const UniValue &result) const {
            if (m_messageSize > 0)
                return m_messageSize;
            return calculateMessageSize(result);
        }
        /// Returns the name of the RPC method we map
        inline const std::string &method() const {
            return m_method;
        }

        /**
         * @brief createRequest takes the incoming \a message and creates a univalue request to pass to the RPC layer.
         * @param message the incoming message.
         * @param output the data that the RPC method() we map to will understand.
         */
        virtual void createRequest(const Message &message, UniValue &output);
        /**
         * @brief The buildReply method takes the results from the RPC method we map to and builds the reply to be sent over the network.
         */
        virtual void buildReply(Streaming::MessageBuilder &builder, const UniValue &result);
        /// Return the size we shall reserve for the message to be created in buildReply.
        /// This size CAN NOT be smaller than what is actually consumed in buildReply.
        virtual int calculateMessageSize(const UniValue&) const;

    protected:
        std::string m_method;
    };

    /**
     * A parser that calls directly to the libraries to execute the API call.
     * When a new request comes in from the network, the specific parser that can handle this
     * is instantiated and then buildReply() is called with the message we received from the network,
     * expecting a reply to be created to be send back to the caller.
     */
    class DirectParser : public Parser {
    public:
        DirectParser(int replyMessageId, int messageSize = -1);

        /// Return the size we shall reserve for the message to be created in buildReply.
        /// This size CAN NOT be smaller than what is actually consumed in buildReply.
        virtual int calculateMessageSize() const { return m_messageSize; }

        /**
         * @brief The buildReply method takes the request and builds the reply to be sent over the network.
         */
        virtual void buildReply(const Message &request, Streaming::MessageBuilder &builder) = 0;
    };

    /// maps an input message to a Parser implementation.
    Parser* createParser(const Message &message);
}

#endif
