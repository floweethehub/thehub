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

#ifndef HTTPENGINE_QIODEVICECOPIER_H
#define HTTPENGINE_QIODEVICECOPIER_H

#include <QObject>

#include "httpengine_export.h"

class QIODevice;

namespace HttpEngine
{

class HTTPENGINE_EXPORT QIODeviceCopierPrivate;

/**
 * @brief Data copier for classes deriving from QIODevice
 *
 * QIODeviceCopier provides a set of methods for reading data from a QIODevice
 * and writing it to another. The class operates asynchronously and therefore
 * can be used from the main thread. The copier is initialized with pointers
 * to two QIODevices:
 *
 * @code
 * QFile srcFile("src.txt");
 * QFile destFile("dest.txt");
 *
 * HttpEngine::QIODeviceCopier copier(&srcFile, &destFile);
 * copier.start()
 * @endcode
 *
 * Notice in the example above that it is not necessary to open the devices
 * prior to starting the copy operation. The copier will attempt to open both
 * devices with the appropriate mode if they are not already open.
 *
 * If the source device is sequential, data will be read as it becomes
 * available and immediately written to the destination device. If the source
 * device is not sequential, data will be read and written in blocks. The size
 * of the blocks can be modified with the setBufferSize() method.
 *
 * If an error occurs, the error() signal will be emitted. When the copy
 * completes, either by reading all of the data from the source device or
 * encountering an error, the finished() signal is emitted.
 */
class HTTPENGINE_EXPORT QIODeviceCopier : public QObject
{
    Q_OBJECT

public:

    /**
     * @brief Create a new device copier from the specified source and destination devices
     */
    QIODeviceCopier(QIODevice *src, QIODevice *dest, QObject *parent = nullptr);

    /**
     * @brief Set the size of the buffer
     */
    void setBufferSize(qint64 size);

    /**
     * @brief Set range of data to copy, if src device is not sequential
     */
    void setRange(qint64 from, qint64 to);

Q_SIGNALS:

    /**
     * @brief Indicate that an error occurred
     */
    void error(const QString &message);

    /**
     * @brief Indicate that the copy operation finished
     *
     * For sequential devices, this will occur when readChannelFinished() is
     * emitted. For other devices, this signal relies on QIODevice::atEnd()
     * and QIODevice::aboutToClose().
     *
     * This signal will also be emitted immediately after the error() signal
     * or if the stop() method is invoked.
     */
    void finished();

public Q_SLOTS:

    /**
     * @brief Start the copy operation
     *
     * The source device will be opened for reading and the destination device
     * opened for writing if applicable. If opening either file fails for some
     * reason, the error() signal will be emitted.
     *
     * This method should never be invoked more than once.
     */
    void start();

    /**
     * @brief Stop the copy operation
     *
     * The start() method should not be invoked after stopping the operation.
     * Instead, a new QIODeviceCopier instance should be created.
     */
    void stop();

private:

    QIODeviceCopierPrivate *const d;
    friend class QIODeviceCopierPrivate;
};

}

#endif
