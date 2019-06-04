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

#include "TestParser.h"
#include <QList>
#include <QObject>
#include <QTest>

#include <httpengine/parser.h>
#include <httpengine/socket.h>
#include <httpengine/ibytearray.h>

typedef QList<QByteArray> QByteArrayList;

Q_DECLARE_METATYPE(HttpEngine::Socket::Method)
Q_DECLARE_METATYPE(HttpEngine::Socket::QueryStringMap)
Q_DECLARE_METATYPE(HttpEngine::Socket::HeaderMap)

const HttpEngine::IByteArray Key1 = "a";
const QByteArray Value1 = "b";
const QByteArray Line1 = Key1 + ": " + Value1;

const HttpEngine::IByteArray Key2 = "c";
const QByteArray Value2 = "d";
const QByteArray Line2 = Key2 + ": " + Value2;

TestParser::TestParser()
{
    headers.insert(Key1, Value1);
    headers.insert(Key2, Value2);
}

void TestParser::testSplit_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QByteArray>("delim");
    QTest::addColumn<int>("maxSplit");
    QTest::addColumn<QByteArrayList>("parts");

    QTest::newRow("empty string")
            << QByteArray()
            << QByteArray(",")
            << 0
            << (QByteArrayList() << "");

    QTest::newRow("no delimiter")
            << QByteArray("a")
            << QByteArray(",")
            << 0
            << (QByteArrayList() << "a");

    QTest::newRow("delimiter")
            << QByteArray("a::b::c")
            << QByteArray("::")
            << 0
            << (QByteArrayList() << "a" << "b" << "c");

    QTest::newRow("empty parts")
            << QByteArray("a,,")
            << QByteArray(",")
            << 0
            << (QByteArrayList() << "a" << "" << "");

    QTest::newRow("maxSplit")
            << QByteArray("a,a,a")
            << QByteArray(",")
            << 1
            << (QByteArrayList() << "a" << "a,a");
}

void TestParser::testSplit()
{
    QFETCH(QByteArray, data);
    QFETCH(QByteArray, delim);
    QFETCH(int, maxSplit);
    QFETCH(QByteArrayList, parts);

    QByteArrayList outParts;
    HttpEngine::Parser::split(data, delim, maxSplit, outParts);

    QCOMPARE(outParts, parts);
}

void TestParser::testParsePath_data()
{
    QTest::addColumn<QByteArray>("rawPath");
    QTest::addColumn<QString>("path");
    QTest::addColumn<HttpEngine::Socket::QueryStringMap>("map");

    QTest::newRow("no query string")
            << QByteArray("/path")
            << QString("/path")
            << HttpEngine::Socket::QueryStringMap();

    QTest::newRow("single parameter")
            << QByteArray("/path?a=b")
            << QString("/path")
            << HttpEngine::Socket::QueryStringMap{{"a", "b"}};
}

void TestParser::testParsePath()
{
    QFETCH(QByteArray, rawPath);
    QFETCH(QString, path);
    QFETCH(HttpEngine::Socket::QueryStringMap, map);

    QString outPath;
    HttpEngine::Socket::QueryStringMap outMap;

    QVERIFY(HttpEngine::Parser::parsePath(rawPath, outPath, outMap));

    QCOMPARE(path, outPath);
    QCOMPARE(map, outMap);
}

void TestParser::testParseHeaderList_data()
{
    QTest::addColumn<bool>("success");
    QTest::addColumn<QByteArrayList>("lines");
    QTest::addColumn<HttpEngine::Socket::HeaderMap>("headers");

    QTest::newRow("empty line")
            << false
            << (QByteArrayList() << "");

    QTest::newRow("multiple lines")
            << true
            << (QByteArrayList() << Line1 << Line2)
            << headers;
}

void TestParser::testParseHeaderList()
{
    QFETCH(bool, success);
    QFETCH(QByteArrayList, lines);

    HttpEngine::Socket::HeaderMap outHeaders;
    QCOMPARE(HttpEngine::Parser::parseHeaderList(lines, outHeaders), success);

    if (success) {
        QFETCH(HttpEngine::Socket::HeaderMap, headers);
        QCOMPARE(outHeaders, headers);
    }
}

void TestParser::testParseHeaders_data()
{
    QTest::addColumn<bool>("success");
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QByteArrayList>("parts");

    QTest::newRow("empty headers")
            << false
            << QByteArray("");

    QTest::newRow("simple GET request")
            << true
            << QByteArray("GET / HTTP/1.0")
            << (QByteArrayList() << "GET" << "/" << "HTTP/1.0");
}

void TestParser::testParseHeaders()
{
    QFETCH(bool, success);
    QFETCH(QByteArray, data);

    QByteArrayList outParts;
    HttpEngine::Socket::HeaderMap outHeaders;

    QCOMPARE(HttpEngine::Parser::parseHeaders(data, outParts, outHeaders), success);

    if (success) {
        QFETCH(QByteArrayList, parts);
        QCOMPARE(outParts, parts);
    }
}

void TestParser::testParseRequestHeaders_data()
{
    QTest::addColumn<bool>("success");
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<HttpEngine::Socket::Method>("method");
    QTest::addColumn<QByteArray>("path");

    QTest::newRow("bad HTTP version")
            << false
            << QByteArray("GET / HTTP/0.9");

    QTest::newRow("GET request")
            << true
            << QByteArray("GET / HTTP/1.0")
            << HttpEngine::Socket::GET
            << QByteArray("/");
}

void TestParser::testParseRequestHeaders()
{
    QFETCH(bool, success);
    QFETCH(QByteArray, data);

    HttpEngine::Socket::Method outMethod;
    QByteArray outPath;
    HttpEngine::Socket::HeaderMap outHeaders;

    QCOMPARE(HttpEngine::Parser::parseRequestHeaders(data, outMethod, outPath, outHeaders), success);

    if (success) {
        QFETCH(HttpEngine::Socket::Method, method);
        QFETCH(QByteArray, path);

        QCOMPARE(method, outMethod);
        QCOMPARE(path, outPath);
    }
}

void TestParser::testParseResponseHeaders_data()
{
    QTest::addColumn<bool>("success");
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<int>("statusCode");
    QTest::addColumn<QByteArray>("statusReason");

    QTest::newRow("invalid status code")
            << false
            << QByteArray("HTTP/1.0 600 BAD RESPONSE");

    QTest::newRow("404 response")
            << true
            << QByteArray("HTTP/1.0 404 NOT FOUND")
            << 404
            << QByteArray("NOT FOUND");
}

void TestParser::testParseResponseHeaders()
{
    QFETCH(bool, success);
    QFETCH(QByteArray, data);

    int outStatusCode;
    QByteArray outStatusReason;
    HttpEngine::Socket::HeaderMap outHeaders;

    QCOMPARE(HttpEngine::Parser::parseResponseHeaders(data, outStatusCode, outStatusReason, outHeaders), success);

    if (success) {
        QFETCH(int, statusCode);
        QFETCH(QByteArray, statusReason);

        QCOMPARE(statusCode, outStatusCode);
        QCOMPARE(statusReason, outStatusReason);
    }
}

