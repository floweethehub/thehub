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

#include "TestQIODeviceCopier.h"
#include <QBuffer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <httpengine/qiodevicecopier.h>

#include "common/qsocketpair.h"

const QByteArray SampleData = "1234567890123456789012345678901234567890";

void TestQIODeviceCopier::testQBuffer()
{
    QBuffer src;
    src.setData(SampleData);

    QByteArray destData;
    QBuffer dest(&destData);

    HttpEngine::QIODeviceCopier copier(&src, &dest);
    copier.setBufferSize(2);

    QSignalSpy errorSpy(&copier, SIGNAL(error(QString)));
    QSignalSpy finishedSpy(&copier, SIGNAL(finished()));

    copier.start();

    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(destData, SampleData);
}

void TestQIODeviceCopier::testQTcpSocket()
{
    QSocketPair pair;
    QTRY_VERIFY(pair.isConnected());

    QByteArray destData;
    QBuffer dest(&destData);

    HttpEngine::QIODeviceCopier copier(pair.server(), &dest);
    copier.setBufferSize(2);

    QSignalSpy errorSpy(&copier, SIGNAL(error(QString)));
    QSignalSpy finishedSpy(&copier, SIGNAL(finished()));

    copier.start();

    pair.client()->write(SampleData);
    pair.client()->close();

    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(destData, SampleData);
}

void TestQIODeviceCopier::testStop()
{
    QSocketPair pair;
    QTRY_VERIFY(pair.isConnected());

    QByteArray destData;
    QBuffer dest(&destData);

    HttpEngine::QIODeviceCopier copier(pair.server(), &dest);

    copier.start();

    pair.client()->write(SampleData);
    QTRY_COMPARE(destData, SampleData);

    copier.stop();

    pair.client()->write(SampleData);
    QTRY_COMPARE(destData, SampleData);
}

void TestQIODeviceCopier::testRange_data()
{
    QTest::addColumn<int>("from");
    QTest::addColumn<int>("to");
    QTest::addColumn<int>("bufferSize");

    QTest::newRow("range: 1-21, bufSize: 8")
            << 1 << 21 << 8;

    QTest::newRow("range: 0-21, bufSize: 7")
            << 0 << 21 << 7;

    QTest::newRow("range: 10-, bufSize: 5")
            << 10 << -1 << 5;
}

void TestQIODeviceCopier::testRange()
{
    QFETCH(int, from);
    QFETCH(int, to);
    QFETCH(int, bufferSize);

    QBuffer src;
    src.setData(SampleData);

    QByteArray destData;
    QBuffer dest(&destData);

    HttpEngine::QIODeviceCopier copier(&src, &dest);
    copier.setBufferSize(bufferSize);
    copier.setRange(from, to);

    QSignalSpy errorSpy(&copier, SIGNAL(error(QString)));
    QSignalSpy finishedSpy(&copier, SIGNAL(finished()));

    copier.start();

    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(destData, SampleData.mid(from, to - from + 1));
}

