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
#include "APIServer.h"
#include "APIRPCBinding.h"
#include "APIProtocol.h"

#include "streaming/MessageBuilder.h"
#include "streaming/MessageParser.h"

#include "chainparamsbase.h"
#include "netbase.h"
#include "util.h"
#include "utilstrencodings.h"
#include "random.h"
#include "rpcserver.h"

#include <fstream>
#include <functional>

// the amount of seconds after which we disconnect incoming connections that have done anything yet.
#define INTRODUCTION_TIMEOUT 4

Api::Server::Server(boost::asio::io_service &service)
    : m_networkManager(service),
      m_timerRunning(false),
      m_newConnectionTimeout(service)
{
    int defaultPort = BaseParams().ApiServerPort();
    using boost::asio::ip::tcp;
    std::list<tcp::endpoint> endpoints;

    if (mapArgs.count("-apilisten")) {
        for (auto strAddress : mapMultiArgs["-apilisten"]) {
            uint16_t port = defaultPort;
            std::string host;
            SplitHostPort(strAddress, port, host);
            if (host.empty()) {
                host = "127.0.0.1";
            } else if (host == "localhost") {
                endpoints.push_back(tcp::endpoint(boost::asio::ip::address_v4::loopback(), port));
                endpoints.push_back(tcp::endpoint(boost::asio::ip::address_v6::loopback(), port));
                continue;
            }
            try {
                endpoints.push_back(tcp::endpoint(boost::asio::ip::address::from_string(host), port));
            } catch (std::runtime_error &e) {
                logCritical(Log::ApiServer) << "Bind port needs to be an API address. Parsing failed with" << e;
            }
        }
    } else {
        endpoints.push_back(tcp::endpoint(boost::asio::ip::address_v4::loopback(), defaultPort));
        endpoints.push_back(tcp::endpoint(boost::asio::ip::address_v6::loopback(), defaultPort));
    }

    for (auto endpoint : endpoints) {
        try {
            m_networkManager.bind(endpoint, std::bind(&Api::Server::newConnection, this, std::placeholders::_1));
            logInfo(Log::ApiServer) << "Api Server listening on" << endpoint;
        } catch (const std::exception &e) {
            logCritical(Log::ApiServer) << "Api Server failed to listen on" << endpoint << "due to:" << e;
        }
    }
}

void Api::Server::addService(NetworkService *service)
{
    m_networkManager.addService(service);
}

void Api::Server::newConnection(NetworkConnection &connection)
{
    boost::mutex::scoped_lock lock(m_mutex);
    logDebug() << "server newConnection";
    NewConnection con;
    connection.setOnIncomingMessage(std::bind(&Api::Server::incomingMessage, this, std::placeholders::_1));
    connection.setOnDisconnected(std::bind(&Api::Server::connectionRemoved, this, std::placeholders::_1));
    connection.accept();
    con.connection = std::move(connection);
    con.initialConnectionTime = boost::posix_time::second_clock::universal_time();

    m_newConnections.push_back(std::move(con));

    if (!m_timerRunning) {
        m_timerRunning = true;
        m_newConnectionTimeout.expires_from_now(boost::posix_time::seconds(INTRODUCTION_TIMEOUT));
        m_newConnectionTimeout.async_wait(std::bind(&Api::Server::checkConnections, this, std::placeholders::_1));
    }
}

void Api::Server::connectionRemoved(const EndPoint &endPoint)
{
    boost::mutex::scoped_lock lock(m_mutex);
    auto iter = m_newConnections.begin();
    while (iter != m_newConnections.end()) {
        if (iter->connection.connectionId() == endPoint.connectionId) {
            m_newConnections.erase(iter);
            break;
        }
        ++iter;
    }

    auto conIter = m_connections.begin();
    while (conIter != m_connections.end()) {
        if ((*conIter)->m_connection.connectionId() == endPoint.connectionId) {
            m_connections.erase(conIter);
            delete *conIter;
            break;
        }
        ++conIter;
    }
}

void Api::Server::incomingMessage(const Message &message)
{
    logDebug() << "incomingMessage";
    Connection *handler;
    {
        boost::mutex::scoped_lock lock(m_mutex);
        bool found = false;
        auto iter = m_newConnections.begin();
        while (iter != m_newConnections.end()) {
            if (iter->connection.connectionId() == message.remote) {
                m_newConnections.erase(iter);
                found = true;
                break;
            }
            ++iter;
        }

        if (!found)
            return;
        NetworkConnection con(&m_networkManager, message.remote);
        assert(con.isValid());
        con.setOnDisconnected(std::bind(&Api::Server::connectionRemoved, this, std::placeholders::_1));

        handler = new Connection(std::move(con));
        m_connections.push_back(handler);
    }
    handler->incomingMessage(message);
}

void Api::Server::checkConnections(boost::system::error_code error)
{
    if (error.value() == boost::asio::error::operation_aborted)
        return;
    boost::mutex::scoped_lock lock(m_mutex);
    const auto disconnectTime = boost::posix_time::second_clock::universal_time()
            - boost::posix_time::seconds(INTRODUCTION_TIMEOUT);
    auto iter = m_newConnections.begin();
    while (iter != m_newConnections.end()) {
        if (iter->initialConnectionTime <= disconnectTime) {
            logDebug() << "Calling disconnect on connection" << iter->connection.connectionId() << "now";
            iter->connection.disconnect();
            iter = m_newConnections.erase(iter);
        } else {
            ++iter;
        }
    }

    // restart timer if there is still something left.
    if (!m_newConnections.empty()) {
        m_timerRunning = true;
        m_newConnectionTimeout.expires_from_now(boost::posix_time::seconds(1));
        m_newConnectionTimeout.async_wait(std::bind(&Api::Server::checkConnections, this, std::placeholders::_1));
    } else {
        m_timerRunning = false;
    }
}


Api::Server::Connection::Connection(NetworkConnection && connection)
    : m_connection(std::move(connection)),
      m_bufferPool(4000000) // default size is 4MB
{
    m_connection.setOnIncomingMessage(std::bind(&Api::Server::Connection::incomingMessage, this, std::placeholders::_1));
}

Api::Server::Connection::~Connection()
{
    for (auto iter = m_properties.begin(); iter != m_properties.end(); ++iter) {
        delete iter->second;
    }
}

void Api::Server::Connection::incomingMessage(const Message &message)
{
    if (message.serviceId() >= 16) // not a service we handle
        return;
    if (message.serviceId() == APIService && message.messageId() == Meta::Version) {
        Streaming::MessageBuilder builder(m_bufferPool);
        std::ostringstream ss;
        ss << "Flowee:" << HUB_SERIES << " (" << CLIENT_VERSION_MAJOR << "-" << CLIENT_VERSION_MINOR
           << "." << CLIENT_VERSION_REVISION << ")";;
        builder.add(Meta::GenericByteData, ss.str());
        m_connection.send(builder.reply(message));
        return;
    }

    std::unique_ptr<Api::Parser> parser;
    try {
        parser.reset(Api::createParser(message));
        assert(parser.get()); // createParser should never return a nullptr
    } catch (const std::exception &e) {
        logWarning(Log::ApiServer) << e;
        sendFailedMessage(message, e.what());
        return;
    }

    assert(parser.get());
    assert(message.serviceId() < 0xFFFF);
    assert(message.messageId() < 0xFFFF);
    const uint32_t sessionDataId = static_cast<uint32_t>(message.serviceId()) << 16 + static_cast<uint32_t>(message.messageId());
    parser.get()->setSessionData(&m_properties[sessionDataId]);

    auto *rpcParser = dynamic_cast<Api::RpcParser*>(parser.get());
    if (rpcParser) {
        assert(!rpcParser->method().empty());
        try {
            UniValue request(UniValue::VOBJ);
            rpcParser->createRequest(message, request);
            UniValue result;
            try {
                logInfo(Log::ApiServer) << rpcParser->method() << message.serviceId() << '/' << message.messageId();
                result = tableRPC.execute(rpcParser->method(), request);
            } catch (UniValue& objError) {
                const std::string error = find_value(objError, "message").get_str();
                logWarning(Log::ApiServer) << error;
                sendFailedMessage(message, error);
                return;
            } catch(const std::exception &e) {
                logWarning(Log::ApiServer) << e;
                sendFailedMessage(message, std::string(e.what()));
                return;
            }
            int reserveSize = rpcParser->messageSize(result);
            m_bufferPool.reserve(reserveSize);
            Streaming::MessageBuilder builder(m_bufferPool);
            rpcParser->buildReply(builder, result);
            Message reply = builder.reply(message, rpcParser->replyMessageId());
            if (reserveSize < reply.body().size())
                logDebug(Log::ApiServer) << "Generated message larger than space reserved."
                                         << message.serviceId() << message.messageId()
                                         << "reserved:" << reserveSize << "built:" << reply.body().size();
            assert(reply.body().size() <= reserveSize); // fail fast.
            m_connection.send(reply);
        } catch (const ParserException &e) {
            logWarning(Log::ApiServer) << e;
            sendFailedMessage(message, e.what());
            return;
        } catch (const std::exception &e) {
            std::string error = "Interal Error " + std::string(e.what());
            logCritical(Log::ApiServer) << "ApiServer internal error in parsing" << rpcParser->method() <<  e;
            (void) m_bufferPool.commit(); // make sure the partial message is discarded
            sendFailedMessage(message, error);
        }
        return;
    }
    auto *directParser = dynamic_cast<Api::DirectParser*>(parser.get());
    assert(directParser);
    if (directParser) {
        int reserveSize = 0;
        try {
            reserveSize = directParser->calculateMessageSize(message);
            m_bufferPool.reserve(reserveSize);
        } catch (const ParserException &e) {
            logWarning(Log::ApiServer) << "calculateMessageSize() threw:" << e;
            sendFailedMessage(message, e.what());
            return;
        }
        logInfo(Log::ApiServer) << message.serviceId() << '/' << message.messageId();
        Streaming::MessageBuilder builder(m_bufferPool);
        try {
            directParser->buildReply(message, builder);
            Message reply = builder.reply(message, directParser->replyMessageId());
            if (reserveSize < reply.body().size())
                logDebug(Log::ApiServer) << "Generated message larger than space reserved."
                                         << message.serviceId() << message.messageId()
                                         << "reserved:" << reserveSize << "built:" << reply.body().size();
            assert(reply.body().size() <= reserveSize); // fail fast.
            m_connection.send(reply);
        } catch (const ParserException &e) {
            logWarning(Log::ApiServer) << e;
            sendFailedMessage(message, e.what());
            return;
        }
    }
}

void Api::Server::Connection::sendFailedMessage(const Message &origin, const std::string &failReason)
{
    m_bufferPool.reserve(failReason.size() + 40);
    Streaming::MessageBuilder builder(m_bufferPool);
    builder.add(Meta::FailedReason, failReason);
    builder.add(Meta::FailedCommandServiceId, origin.serviceId());
    builder.add(Meta::FailedCommandId, origin.messageId());
    Message answer = builder.message(APIService, Meta::CommandFailed);
    const int requestId = origin.headerInt(RequestId);
    if (requestId != -1)
        answer.setHeaderInt(RequestId, requestId);
    m_connection.send(answer);
}



Api::SessionData::~SessionData()
{
}
