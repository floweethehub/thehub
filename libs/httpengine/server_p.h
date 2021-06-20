/* This file is part of Flowee
 *
 * Copyright (C) 2017 Nathan Osman
 * Copyright (C) 2019 Tom Zander <tom@flowee.org>
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

#ifndef HTTPENGINE_SERVER_P_H
#define HTTPENGINE_SERVER_P_H

#include "server.h"

#include <QObject>
#include <QTcpSocket>
#include <QThreadPool>

#if !defined(QT_NO_SSL)
#  include <QSslConfiguration>
#endif

namespace HttpEngine
{

class Handler;

class ServerPrivate : public QObject
{
    Q_OBJECT

public:
    explicit ServerPrivate(Server *httpServer);
    ~ServerPrivate();

    void schedule(qintptr socketDescriptor);

#if !defined(QT_NO_SSL)
    QSslConfiguration configuration;
#endif

    Server *const q;
    std::function<void(WebRequest*)> func;
    QList<QThread*> threads;
    int nextWorker = 0;

};

}

#endif
