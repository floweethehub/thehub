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

#include <QGenericArgument>
#include <QMetaMethod>

#include "qobjecthandler.h"
#include "socket.h"

#include "qobjecthandler_p.h"

using namespace HttpEngine;

QObjectHandlerPrivate::QObjectHandlerPrivate(QObjectHandler *handler)
    : QObject(handler),
      q(handler)
{
}

QObjectHandler::QObjectHandler(QObject *parent)
    : Handler(parent),
      d(new QObjectHandlerPrivate(this))
{
}

void QObjectHandlerPrivate::invokeSlot(Socket *socket, Method m)
{
    // Invoke the slot
    if (m.oldSlot) {

        // Obtain the slot index
        int index = m.receiver->metaObject()->indexOfSlot(m.slot.method + 1);
        if (index == -1) {
            socket->writeError(Socket::InternalServerError);
            return;
        }

        QMetaMethod method = m.receiver->metaObject()->method(index);

        // Ensure the parameter is correct
        QList<QByteArray> params = method.parameterTypes();
        if (params.count() != 1 || params.at(0) != "HttpEngine::Socket*") {
            socket->writeError(Socket::InternalServerError);
            return;
        }

        // Invoke the method
        if (!m.receiver->metaObject()->method(index).invoke(
                    m.receiver, Q_ARG(Socket*, socket))) {
            socket->writeError(Socket::InternalServerError);
            return;
        }
    } else {
        void *args[] = {
            Q_NULLPTR,
            &socket
        };
        m.slot.slotObj->call(m.receiver, args);
    }
}

void QObjectHandler::process(Socket *socket, const QString &path)
{
    // Ensure the method has been registered
    if (!d->map.contains(path)) {
        socket->writeError(Socket::NotFound);
        return;
    }

    QObjectHandlerPrivate::Method m = d->map.value(path);

    // If the slot requires all data to be received, check to see if this is
    // already the case, otherwise, wait until the rest of it arrives
    if (!m.readAll || socket->bytesAvailable() >= socket->contentLength()) {
        d->invokeSlot(socket, m);
    } else {
        connect(socket, &Socket::readChannelFinished, [this, socket, m]() {
            d->invokeSlot(socket, m);
        });
    }
}

void QObjectHandler::registerMethod(const QString &name, QObject *receiver, const char *method, bool readAll)
{
    d->map.insert(name, QObjectHandlerPrivate::Method(receiver, method, readAll));
}

void QObjectHandler::registerMethodImpl(const QString &name, QObject *receiver, QtPrivate::QSlotObjectBase *slotObj, bool readAll)
{
    d->map.insert(name, QObjectHandlerPrivate::Method(receiver, slotObj, readAll));
}
