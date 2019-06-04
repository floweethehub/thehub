/*
 * This file is part of the Flowee project
 * Copyright (c) 2017 Aleksei Ermakov
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

#include "TestRange.h"
#include <QString>
#include <QTest>

#include <httpengine/range.h>

TestRange::TestRange()
{
}

void TestRange::testDefaultConstructor()
{
    HttpEngine::Range range;

    QCOMPARE(range.isValid(), false);
}

void TestRange::testAssignmentOperator()
{
    HttpEngine::Range range;
    HttpEngine::Range otherRange(100, 200, -1);

    range = otherRange;

    QCOMPARE(range.isValid(), true);
    QCOMPARE(range.from(), 100);
    QCOMPARE(range.to(), 200);
}

void TestRange::testFromToLength_data()
{
    QTest::addColumn<int>("inFrom");
    QTest::addColumn<int>("inTo");
    QTest::addColumn<int>("inDataSize");
    QTest::addColumn<int>("from");
    QTest::addColumn<int>("to");
    QTest::addColumn<int>("length");

    QTest::newRow("Last 500 bytes")
            << -500 << -1 << -1
            << -500 << -1 << 500;

    QTest::newRow("Last 500 bytes with 800 dataSize")
            << -500 << -1 << 800
            << 300 << 799 << 500;

    QTest::newRow("Skip first 10 bytes")
            << 10 << -1 << -1
            << 10 << -1 << -1;

    QTest::newRow("Skip first 10 bytes with 100 dataSize")
            << 10 << -1 << 100
            << 10 << 99 << 90;
}

void TestRange::testFromToLength()
{
    QFETCH(int, inFrom);
    QFETCH(int, inTo);
    QFETCH(int, inDataSize);
    QFETCH(int, from);
    QFETCH(int, to);
    QFETCH(int, length);

    HttpEngine::Range range(inFrom, inTo, inDataSize);

    QCOMPARE(range.from(), from);
    QCOMPARE(range.to(), to);
    QCOMPARE(range.length(), length);
}

void TestRange::testIsValid_data()
{
    QTest::addColumn<int>("from");
    QTest::addColumn<int>("to");
    QTest::addColumn<int>("dataSize");
    QTest::addColumn<bool>("valid");

    QTest::newRow("Normal range")
            << 0 << 100 << -1 << true;

    QTest::newRow("Normal range with 'dataSize'")
            << 0 << 99 << 100 << true;

    QTest::newRow("Last N bytes")
            << -500 << -1 << -1 << true;

    QTest::newRow("Last N bytes with 'dataSize'")
            << -500 << -1 << 500 << true;

    QTest::newRow("Skip first N bytes")
            << 10 << -1 << -1 << true;

    QTest::newRow("Skip first N bytes with 'dataSize'")
            << 10 << -1 << 500 << true;

    QTest::newRow("OutOfBounds 'to' > 'from'")
            << 100 << 50 << -1 << false;

    QTest::newRow("OutOfBounds 'from' > 'dataSize'")
            << 100 << 200 << 150 << false;

    QTest::newRow("Last N bytes where N > 'dataSize'")
            << -500 << -1 << 499 << false;

    QTest::newRow("Skip first N bytes where N > 'dataSize'")
            << 500 << -1 << 499 << false;
}

void TestRange::testIsValid()
{
    QFETCH(int, from);
    QFETCH(int, to);
    QFETCH(int, dataSize);
    QFETCH(bool, valid);

    HttpEngine::Range range(from, to, dataSize);

    QCOMPARE(range.isValid(), valid);
}

void TestRange::testParseFromString_data()
{
    QTest::addColumn<QString>("data");
    QTest::addColumn<int>("dataSize");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<int>("from");
    QTest::addColumn<int>("to");
    QTest::addColumn<int>("length");

    QTest::newRow("Normal range")
            << "0-99" << -1
            << true << 0 << 99 << 100;

    QTest::newRow("Normal range with 'dataSize'")
            << "0-99" << 100
            << true << 0 << 99 << 100;

    QTest::newRow("Last N bytes")
            << "-256" << -1
            << true << -256 << -1 << 256;

    QTest::newRow("Last N bytes with 'dataSize'")
            << "-256" << 256
            << true << 0 << 255 << 256;

    QTest::newRow("Skip first N bytes")
            << "100-" << -1
            << true << 100 << -1 << -1;

    QTest::newRow("Skip first N bytes with 'dataSize'")
            << "100-" << 200
            << true << 100 << 199 << 100;

    QTest::newRow("OutOfBounds 'to' > 'from'")
            << "100-50" << -1
            << false;

    QTest::newRow("OutOfBounds 'from' > 'dataSize'")
            << "0-200" << 100
            << false;

    QTest::newRow("Last N bytes where N > 'dataSize'")
            << "-500" << 200
            << false;

    QTest::newRow("Skip first N bytes where N > 'dataSize'")
            << "100-" << 100
            << false;

    QTest::newRow("Bad input: '-'")
            << "-" << -1
            << false;

    QTest::newRow("Bad input: 'abc-def'")
            << "abc-def" << -1
            << false;

    QTest::newRow("Bad input: 'abcdef'")
            << "abcdef" << -1
            << false;
}

void TestRange::testParseFromString()
{
    QFETCH(QString, data);
    QFETCH(int, dataSize);
    QFETCH(bool, valid);

    HttpEngine::Range range(data, dataSize);

    QCOMPARE(range.isValid(), valid);

    if (valid) {
        QFETCH(int, from);
        QFETCH(int, to);
        QFETCH(int, length);

        QCOMPARE(range.from(), from);
        QCOMPARE(range.to(), to);
        QCOMPARE(range.length(), length);
    }
}

void TestRange::testContentRange_data()
{
    QTest::addColumn<int>("from");
    QTest::addColumn<int>("to");
    QTest::addColumn<int>("dataSize");
    QTest::addColumn<QString>("contentRange");

    QTest::newRow("Normal range with 'dataSize'")
            << 0 << 100 << 1000
            << "0-100/1000";

    QTest::newRow("Normal range without 'dataSize'")
            << 0 << 100 << -1
            << "0-100/*";

    QTest::newRow("Invalid range with 'dataSize'")
            << 100 << 10 << 1200
            << "*/1200";

    QTest::newRow("Invalid range without 'dataSize'")
            << 100 << 10 << -1
            << "";
}

void TestRange::testContentRange()
{
    QFETCH(int, from);
    QFETCH(int, to);
    QFETCH(int, dataSize);
    QFETCH(QString, contentRange);

    HttpEngine::Range range(from, to, dataSize);

    QCOMPARE(range.contentRange(), contentRange);
}

