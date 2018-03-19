/*
 * This file is part of the Flowee project
 * Copyright (C) 2016 Tom Zander <tomz@freedommail.ch>
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

// the amount of seconds after which we disconnect incoming connections that have not logged in yet.
#define LOGIN_TIMEOUT 4

Api::Server::Server(boost::asio::io_service &service)
    : m_networkManager(service),
      m_timerRunning(false),
      m_newConnectionTimeout(service)
{
    boost::filesystem::path path(GetArg("-apicookiefile", "api-cookie"));
    if (!path.is_complete())
        path = GetDataDir() / path;

    std::ifstream file;
    file.open(path.string().c_str());
    if (file.is_open()) {
        std::getline(file, m_cookie);
        file.close();
    } else {
        // then we create one.
        uint8_t buf[32];
        GetRandBytes(buf, 32);
        m_cookie = EncodeBase64(&buf[0],32);

        std::ofstream out;
        out.open(path.string().c_str());
        if (!out.is_open()) {
            logFatal(Log::ApiServer) << "Unable to open api-cookie authentication file" << path.string() << "for writing";
            throw std::runtime_error("Unable to open api-cookie authentication file.");
        }
        out << m_cookie;
        out.close();
        logInfo(Log::ApiServer) << "Generated api-authentication cookie" << path.string();
    }

    int defaultPort = BaseParams().ApiServerPort();
    std::list<boost::asio::ip::tcp::endpoint> endpoints;

    if (mapArgs.count("-apilisten")) {
        for (auto strAddress : mapMultiArgs["-apilisten"]) {
            int port = defaultPort;
            std::string host;
            SplitHostPort(strAddress, port, host);
            if (host.empty())
                host = "127.0.0.1";
            endpoints.push_back(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(host), port));
        }
    } else {
        endpoints.push_back(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string("127.0.0.1"), defaultPort));
        endpoints.push_back(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string("::1"), defaultPort));
    }

    for (auto endpoint : endpoints) {
        try {
            m_networkManager.bind(endpoint, std::bind(&Api::Server::newConnection, this, std::placeholders::_1));
            LogPrintf("Api Server listening on %s\n", endpoint);
        } catch (const std::exception &e) {
            LogPrintf("Api Server failed to listen on %s. %s", endpoint, e.what());
        }
    }
}

void Api::Server::addService(NetworkService *service)
{
    m_networkManager.addService(service);
}

void Api::Server::newConnection(NetworkConnection &connection)
{
    connection.setOnIncomingMessage(std::bind(&Api::Server::incomingLoginMessage, this, std::placeholders::_1));
    connection.setOnDisconnected(std::bind(&Api::Server::connectionRemoved, this, std::placeholders::_1));
    connection.accept();
    NewConnection con;
    con.connection = std::move(connection);
    con.time = boost::posix_time::second_clock::universal_time() + boost::posix_time::seconds(LOGIN_TIMEOUT);

    boost::mutex::scoped_lock lock(m_mutex);
    m_newConnections.push_back(std::move(con));

    if (!m_timerRunning) {
        m_timerRunning = true;
        m_newConnectionTimeout.expires_from_now(boost::posix_time::seconds(LOGIN_TIMEOUT));
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

void Api::Server::incomingLoginMessage(const Message &message)
{
    bool success = false;
    std::string error;
    if (message.messageId() == Login::LoginMessage && message.serviceId() == LoginService) {
        Streaming::MessageParser parser(message.body());
        while (!success && parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Login::CookieData) {
                assert(!m_cookie.empty());
                if (m_cookie == parser.stringData())
                    success = true;
                else if (parser.dataLength() != 44)
                    error = strprintf("Cookie wrong length; %d", parser.dataLength());
                else
                    error = parser.stringData() + "|"+ m_cookie;
            }
        }
    }
    NetworkConnection con(&m_networkManager, message.remote);
    assert(con.isValid());
    if (!success) {
        if (error.empty())
            error = "Malformed login, no cookie data found";
        logCritical(Log::ApiServer) << "Remote failed login" << error;
        con.disconnect();
        return;
    }
    logInfo(Log::ApiServer) << "Remote login accepted from" << con.endPoint().hostname << con.endPoint().ipAddress.to_string();

    con.setOnDisconnected(std::bind(&Api::Server::connectionRemoved, this, std::placeholders::_1));
    Connection *handler = new Connection(std::move(con));
    boost::mutex::scoped_lock lock(m_mutex);
    m_connections.push_back(handler);

    auto iter = m_newConnections.begin();
    while (iter != m_newConnections.end()) {
        if (iter->connection.connectionId() == message.remote) {
            m_newConnections.erase(iter);
            break;
        }
        ++iter;
    }
}

void Api::Server::checkConnections(boost::system::error_code error)
{
    if (error.value() == boost::asio::error::operation_aborted)
        return;
    boost::mutex::scoped_lock lock(m_mutex);
    const auto now = boost::posix_time::second_clock::universal_time();
    auto iter = m_newConnections.begin();
    while (iter != m_newConnections.end()) {
        if (iter->time <= now) {
            // LogPrintf("Calling disconnect on connection %d now\n", iter->connection.connectionId());
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
    : m_connection(std::move(connection))
{
    m_connection.setOnIncomingMessage(std::bind(&Api::Server::Connection::incomingMessage, this, std::placeholders::_1));
}

void Api::Server::Connection::incomingMessage(const Message &message)
{
    std::unique_ptr<APIRPCBinding::Parser> parser;
    try {
        parser.reset(APIRPCBinding::createParser(message));
        assert(parser.get()); // createParser should never return a nullptr
    } catch (const std::exception &e) {
        logWarning(Log::ApiServer) << e;
        sendFailedMessage(message, e.what());
        return;
    }

    assert(parser.get());

    auto *rpcParser = dynamic_cast<APIRPCBinding::RpcParser*>(parser.get());
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
            m_bufferPool.reserve(rpcParser->messageSize(result));
            Streaming::MessageBuilder builder(m_bufferPool);
            rpcParser->buildReply(builder, result);
            Message reply = builder.message(message.serviceId(), rpcParser->replyMessageId());
            const int requestId = message.headerInt(Api::RequestId);
            if (requestId != -1)
                reply.setHeaderInt(Api::RequestId, requestId);
            m_connection.send(reply);
        } catch (const std::exception &e) {
            std::string error = "Interal Error " + std::string(e.what());
            logCritical(Log::ApiServer) << "ApiServer internal error in parsing" << rpcParser->method() <<  e;
            (void) m_bufferPool.commit(); // make sure the partial message is discarded
            sendFailedMessage(message, error);
        }
        return;
    }
    auto *directParser = dynamic_cast<APIRPCBinding::DirectParser*>(parser.get());
    if (directParser) {
        m_bufferPool.reserve(directParser->calculateMessageSize());
        Streaming::MessageBuilder builder(m_bufferPool);
        directParser->buildReply(message, builder);
        Message reply = builder.message(message.serviceId(), directParser->replyMessageId());
        const int requestId = message.headerInt(Api::RequestId);
        if (requestId != -1)
            reply.setHeaderInt(Api::RequestId, requestId);
        m_connection.send(reply);
    }
}

void Api::Server::Connection::sendFailedMessage(const Message &origin, const std::string &failReason)
{
    m_bufferPool.reserve(failReason.size() + 20);
    Streaming::MessageBuilder builder(m_bufferPool);
    builder.add(Control::FailedReason, failReason);
    builder.add(Control::FailedCommandServiceId, origin.serviceId());
    builder.add(Control::FailedCommandId, origin.messageId());
    Message answer = builder.message(ControlService, Control::CommandFailed);
    const int requestId = origin.headerInt(Api::RequestId);
    if (requestId != -1)
        answer.setHeaderInt(Api::RequestId, requestId);
    m_connection.send(answer);
}
