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

#ifndef HTTPENGINE_QOBJECTHANDLER_P_H
#define HTTPENGINE_QOBJECTHANDLER_P_H

#include <QMap>
#include <QObject>

namespace HttpEngine
{

class Socket;
class QObjectHandler;

class QObjectHandlerPrivate : public QObject
{
    Q_OBJECT

public:

    explicit QObjectHandlerPrivate(QObjectHandler *handler);

    // In order to invoke the slot, a "pointer" to it needs to be stored in a
    // map that lets us look up information by method name

    class Method {
    public:
        Method() {}
        Method(QObject *receiver, const char *method, bool readAll)
            : receiver(receiver), oldSlot(true), slot(method), readAll(readAll) {}
        Method(QObject *receiver, QtPrivate::QSlotObjectBase *slotObj, bool readAll)
            : receiver(receiver), oldSlot(false), slot(slotObj), readAll(readAll) {}

        QObject *receiver;
        bool oldSlot;
        union slot{
            slot() {}
            slot(const char *method) : method(method) {}
            slot(QtPrivate::QSlotObjectBase *slotObj) : slotObj(slotObj) {}
            const char *method;
            QtPrivate::QSlotObjectBase *slotObj;
        } slot;
        bool readAll;
    };

    void invokeSlot(Socket*socket, Method m);

    QMap<QString, Method> map;

private:

    QObjectHandler *const q;
};

}

#endif
