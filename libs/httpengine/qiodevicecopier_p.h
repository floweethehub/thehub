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

#ifndef HTTPENGINE_QIODEVICECOPIER_P_H
#define HTTPENGINE_QIODEVICECOPIER_P_H

#include <QObject>

class QIODevice;

namespace HttpEngine
{

class QIODeviceCopier;

class QIODeviceCopierPrivate : public QObject
{
    Q_OBJECT

public:

    QIODeviceCopierPrivate(QIODeviceCopier *copier, QIODevice *srcDevice, QIODevice *destDevice);

    QIODevice *const src;
    QIODevice *const dest;

    qint64 bufferSize;

    qint64 rangeFrom;
    qint64 rangeTo;

public Q_SLOTS:

    void onReadyRead();
    void onReadChannelFinished();

    void nextBlock();

private:

    QIODeviceCopier *const q;
};

}

#endif
