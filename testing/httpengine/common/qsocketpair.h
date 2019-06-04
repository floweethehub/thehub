/*
 * This file is part of the Flowee project
 * Copyright (c) 2017 Nathan Osman
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

#ifndef HTTPENGINE_QSOCKETPAIR_H
#define HTTPENGINE_QSOCKETPAIR_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

/**
 * @brief Create a pair of connected TCP sockets
 *
 * This class simplifies the process of creating two TCP sockets that are
 * connected to each other. Once isConnected() returns true, data can be
 * written and read from each socket.
 */
class QSocketPair : public QObject
{
    Q_OBJECT

public:

    QSocketPair();

    bool isConnected() const {
        return mClientSocket.isValid() && mServerSocket && mServerSocket->isValid();
    }

    QTcpSocket *client() {
        return &mClientSocket;
    }

    QTcpSocket *server() {
        return mServerSocket;
    }

private Q_SLOTS:

    void onNewConnection();

private:

    QTcpServer mServer;

    QTcpSocket mClientSocket;
    QTcpSocket *mServerSocket;
};

#endif
