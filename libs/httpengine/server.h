/* This file is part of Flowee
 *
 * Copyright (C) 2017 Nathan Osman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * For the full copy of the License see <http://www.gnu.org/licenses/>
 */

#ifndef HTTPENGINE_SERVER_H
#define HTTPENGINE_SERVER_H

#include <QHostAddress>
#include <QObject>
#include <QTcpServer>

#include "httpengine_export.h"

#include <functional>

#if !defined(QT_NO_SSL)
#include <QSslConfiguration>
#endif

namespace HttpEngine
{
class ServerPrivate;
class WebRequest;
class Socket;

class HTTPENGINE_EXPORT WebRequest : public QObject
{
    Q_OBJECT
public:
    WebRequest(qintptr socketDescriptor, std::function<void(WebRequest*)> &handler);
    ~WebRequest();

    Socket *socket() const;
    QString path() const;

public slots:
    void start();

protected:
    Socket *m_socket;

private:
    void startHttpParsing(QTcpSocket *socket);

    qintptr m_socketDescriptor;
    std::function<void(WebRequest*)> m_handler;

#if !defined(QT_NO_SSL)
public:
    /**
     * @brief Set the SSL configuration for the server
     *
     * If the configuration is not NULL, the server will begin negotiating
     * connections using SSL/TLS.
     */
    void setSslConfiguration(const QSslConfiguration &configuration);

private:
    QSslConfiguration m_ssl;
#endif
};


/**
 * @brief TCP server for HTTP requests
 *
 * This class provides a TCP server that listens for HTTP requests on the
 * specified address and port. When a new request is received, a
 * [Socket](@ref HttpEngine::Socket) is created for the QTcpSocket which
 * abstracts a TCP server socket. Once the request headers are received, the
 * root handler is invoked and the request processed. The server assumes
 * ownership of the QTcpSocket.
 *
 * Because [Server](@ref HttpEngine::Server) derives from QTcpServer,
 * instructing the server to listen on an available port is as simple as
 * invoking listen() with no parameters:
 *
 * @code
 * HttpEngine::Server server;
 * if (!server.listen()) {
 *     // error handling
 * }
 * @endcode
 *
 * Before passing the socket to the handler, the QTcpSocket's disconnected()
 * signal is connected to the [Socket](@ref HttpEngine::Socket)'s
 * deleteLater() slot to ensure that the socket is deleted when the client
 * disconnects.
 */
class HTTPENGINE_EXPORT Server : public QTcpServer
{
    Q_OBJECT

public:
    /**
     * @brief Create an HTTP server
     */
    Server();
    /**
     * @brief Create an HTTP server with the specified handler
     */
    Server(const std::function<void(WebRequest*)> &handler, QObject *parent = nullptr);

    /**
     * Create a WebRequest instance.
     *
     * For every incoming request we create a WebRequest object that will
     * provide the context to handle the request, on its own thread.
     * The default implementation will simply create the baseclass but
     * users may want to create a subclass that they get handed in order
     * to provide all the context they need in their apps.
     */
    virtual WebRequest *createRequest(qintptr socketDescriptor);

#if !defined(QT_NO_SSL)
    /**
     * @brief Set the SSL configuration for the server
     *
     * If the configuration is not NULL, the server will begin negotiating
     * connections using SSL/TLS.
     */
    void setSslConfiguration(const QSslConfiguration &configuration);
#endif

protected:
    /**
     * @brief Implementation of QTcpServer::incomingConnection()
     */
    void incomingConnection(qintptr socketDescriptor) override;

private:
    ServerPrivate *const d;
};

}

#endif
