/* This file is part of Flowee
 *
 * Copyright (C) 2017 Nathan Osman
 * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
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

#include "server_p.h"

#if !defined(QT_NO_SSL)
#  include <QSslSocket>
#endif
#include <QTimer>

#include "handler.h"
#include "socket.h"

using namespace HttpEngine;

ServerPrivate::ServerPrivate(Server *httpServer)
    : QObject(httpServer),
      q(httpServer)
{
    for (int i = 0; i < QThread::idealThreadCount(); ++i) {
        QThread *t = new QThread();
        t->setObjectName("HttpWorker");
        t->start();
        threads.append(t);
    }
}

ServerPrivate::~ServerPrivate()
{
    for (auto t : threads) {
        t->quit();
    }
    for (auto t : threads) {
        t->wait();
        delete t;
    }
    threads.clear();
}

void ServerPrivate::schedule(qintptr socketDescriptor)
{
}

Server::Server()
    : d(new ServerPrivate(this))
{
}

Server::Server(const std::function<void(WebRequest*)> &handler, QObject *parent)
    : QTcpServer(parent),
      d(new ServerPrivate(this))
{
    d->func = handler;
}

#if !defined(QT_NO_SSL)
void Server::setSslConfiguration(const QSslConfiguration &configuration)
{
    d->configuration = configuration;
}
#endif

WebRequest *HttpEngine::Server::createRequest(qintptr socketDescriptor)
{
    return new WebRequest(socketDescriptor, d->func);
}

void Server::incomingConnection(qintptr socketDescriptor)
{
    Q_ASSERT(d->nextWorker < d->threads.size());
    QThread *t = d->threads.at(d->nextWorker);
    auto request = createRequest(socketDescriptor);
    request->setSslConfiguration(d->configuration);
    request->moveToThread(t);
    QTimer::singleShot(0, request, SLOT(start()));
    if (++d->nextWorker >= d->threads.size())
        d->nextWorker = 0;
}

WebRequest::WebRequest(qintptr socketDescriptor, std::function<void(HttpEngine::WebRequest*)> &handler)
    : m_socket(nullptr),
    m_socketDescriptor(socketDescriptor),
    m_handler(handler)
{
}

WebRequest::~WebRequest()
{
}

void WebRequest::start()
{
#if !defined(QT_NO_SSL)
    if (!m_ssl.isNull()) {
        // Initialize the socket with the SSL configuration
        QSslSocket *socket = new QSslSocket(this);

        // Wait until encryption is complete before processing the socket
        QObject::connect(socket, &QSslSocket::encrypted, [this, socket]() {
            startHttpParsing(socket);
        });
        // If an error occurs, delete the socket
        QObject::connect(socket, static_cast<void(QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error),
            socket, &QSslSocket::deleteLater);

        socket->setSocketDescriptor(m_socketDescriptor);
        socket->setSslConfiguration(m_ssl);
        socket->startServerEncryption();
    }
    else
#endif
    {
        QTcpSocket *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(m_socketDescriptor);
        startHttpParsing(socket);
    }

}

Socket *WebRequest::socket() const
{
    return m_socket;
}

QString WebRequest::path() const
{
    return m_socket->path();
}

void WebRequest::startHttpParsing(QTcpSocket *socket)
{
    m_socket = new Socket(socket, this);

    // Wait until the socket finishes reading the HTTP headers before routing
    connect(m_socket, &Socket::headersParsed, [this]() {
        try {
            m_handler(this);
        } catch (const std::exception &e) {
            qWarning() << e.what();
            m_socket->writeError(Socket::InternalServerError);
        }
    });
}

void WebRequest::setSslConfiguration(const QSslConfiguration &configuration)
{
    m_ssl = configuration;
}
