/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2018 Tom Zander <tomz@freedommail.ch>
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

#include "TestBuffers.h"
#include <streaming/BufferPool.h>
#include <streaming/MessageBuilder.h>
#include <streaming/MessageBuilder_p.h>
#include <streaming/MessageParser.h>
#include <limits.h>

using namespace Streaming;

void TestBuffers::testBasic()
{
    Streaming::BufferPool pool;
    pool.reserve(1000);
    const int maxCapacity = pool.capacity();
    QVERIFY(pool.capacity() >= 1000);
    pool.markUsed(101);
    QVERIFY(pool.capacity() == maxCapacity - 101);
    pool.markUsed(122);
    const int newCapacity = maxCapacity - 101 - 122;
    QVERIFY(pool.capacity() == newCapacity);
    Streaming::ConstBuffer buf1 = pool.commit();
    QVERIFY(pool.capacity() == newCapacity);
    QVERIFY(buf1.size() == 223);

    Streaming::ConstBuffer buf2 = pool.commit(pool.capacity());
    QVERIFY(buf1.size() == 223);
    QVERIFY(buf2.size() == newCapacity);
    QVERIFY(pool.capacity() == 0);
}

void TestBuffers::testMultiBuffer()
{
    Streaming::BufferPool pool(500); // small :)
    QVERIFY(pool.capacity() == 500);
    pool.reserve(1000); // bigger!
    QVERIFY(pool.capacity() >= 1000);

    Streaming::ConstBuffer buf1 = pool.commit(800);
    QVERIFY(pool.capacity() == 200);
    pool.reserve(1000); // won't fit. It should create a new buf.
    QVERIFY(pool.capacity() >= 1000);
    Streaming::ConstBuffer buf2 = pool.commit(800);
    QVERIFY(pool.capacity() >= 200);

    QVERIFY(buf1.internal_buffer() != buf2.internal_buffer());
}

void TestBuffers::testBuilder()
{
    MessageBuilder builder(NoHeader);
    builder.add(1, "bla");
    ConstBuffer buf1 = builder.buffer();
    QVERIFY(buf1.size() == 5);
    const char * data = buf1.begin();
    // for (int i = 0; i < 5; ++i) { printf(" %d: %d\n", i, data[i]); }
    QVERIFY(data[0] == 10); // my 1 + 010 (for string) is binary 1010, is decimal 10
    QVERIFY(data[1] == 3); // length of 'bla'
    QVERIFY(data[2] == 'b');
    QVERIFY(data[3] == 'l');
    QVERIFY(data[4] == 'a');
}

void TestBuffers::testParser()
{
    MessageBuilder builder(NoHeader);
    builder.add(1, "bla");
    builder.add(3, 100);
    builder.add(5, true);
    builder.add(100, false);
    std::vector<char> data;
    data.push_back(5);
    data.push_back(0);
    data.push_back(8);
    data.push_back(254);
    builder.add(6, data);
    builder.add(9, 15.5);
    uint256 origHash;
    origHash.SetHex("1298709234abd981729817291a8798172f871982a798195278312095a7982348");
    builder.add(10, origHash);
    ConstBuffer buf = builder.buffer();
    // printf("size %d\n", buf.size());
    QVERIFY(buf.size() == 59);

    MessageParser parser(buf);
    ParsedType type = parser.next();
    QVERIFY(type == FoundTag);
    QVERIFY(parser.tag() == 1);
    variant v = parser.data();
    QVERIFY(boost::get<std::string>(v) == std::string("bla"));

    type = parser.next();
    QVERIFY(parser.tag() == 3);
    QVERIFY(type == FoundTag);
    QVERIFY(parser.isLong() || parser.isInt());
    QVERIFY(parser.intData() == 100);

    type = parser.next();
    QVERIFY(parser.tag() == 5);
    QVERIFY(type == FoundTag);
    QVERIFY(parser.isBool());
    QVERIFY(parser.boolData() == true);

    type = parser.next();
    QVERIFY(parser.tag() == 100);
    QVERIFY(type == FoundTag);
    QVERIFY(parser.isBool());
    QVERIFY(parser.boolData() == false);

    type = parser.next();
    QVERIFY(parser.tag() == 6);
    QVERIFY(type == FoundTag);
    QVERIFY(parser.isByteArray());
    v = parser.data();
    std::vector<char> byteArray = boost::get<std::vector<char> >(v);
    QVERIFY(byteArray == data);

    type = parser.next();
    QVERIFY(parser.tag() == 9);
    QVERIFY(type == FoundTag);
    QVERIFY(parser.isDouble());
    v = parser.data();
    double doubleData = boost::get<double>(v);
    QVERIFY(doubleData == 15.5);
    QVERIFY(parser.doubleData() == 15.5);

    type = parser.next();
    QVERIFY(parser.tag() == 10);
    QVERIFY(type == FoundTag);
    QVERIFY(parser.isByteArray());
    uint256 hash(parser.unsignedBytesData());
    QVERIFY(origHash == hash);

    type = parser.next();
    QVERIFY(type == EndOfDocument);
}

void TestBuffers::testStringRefInParser()
{
    MessageBuilder builder(NoHeader);
    builder.add(1, "bla");
    builder.add(5, "String");
    ConstBuffer buf = builder.buffer();
    QVERIFY(buf.size() == 13);

    MessageParser parser(buf);
    ParsedType type = parser.next();
    QVERIFY(type == FoundTag);
    QVERIFY(parser.tag() == 1);
    QVERIFY(parser.isString());
    boost::string_ref ref = parser.rstringData();
    QVERIFY(ref.length() == 3);
    QVERIFY(ref == std::string("bla"));

    type = parser.next();
    QVERIFY(type == FoundTag);
    QVERIFY(parser.tag() == 5);
    ref = parser.rstringData();
    QVERIFY(ref.length() == 6);
    QVERIFY(ref == std::string("String"));
    QVERIFY(parser.isString());

    type = parser.next();
    QVERIFY(type == EndOfDocument);
}

void TestBuffers::testClear()
{
    BufferPool pool(30000);
    pool.reserve(40000);
    int maxCapacity = pool.capacity();
    QVERIFY(maxCapacity >= 40000);
    pool.markUsed(1000);
    QVERIFY(pool.capacity() == maxCapacity - 1000);

    pool.commit(1000);
    QVERIFY(pool.capacity() == maxCapacity - 2000);

    pool.clear();
    QVERIFY(pool.capacity() == 30000);
    QVERIFY(pool.begin() == nullptr);
    QVERIFY(pool.end() == nullptr);

    pool.reserve(1000);
    QVERIFY(pool.capacity() == 30000);
    QVERIFY(pool.begin() != nullptr);
    QVERIFY(pool.end() != nullptr);

    strcpy(pool.begin(), "bla");
    ConstBuffer buf = pool.commit(4);
    QVERIFY(strncmp(buf.begin(), "bla", 3) == 0);
}

void TestBuffers::testCMFBasic()
{
    MessageBuilder builder(NoHeader);
    builder.add(15, 6512);
    ConstBuffer buf = builder.buffer();
    const char * data = buf.begin();
    QCOMPARE(buf.size(), 3);
    QCOMPARE(data[0], 120);
    QCOMPARE(static_cast<unsigned char>(data[1]), 177);
    QCOMPARE(data[2], 112);

    MessageParser parser(buf);
    ParsedType type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), 15);
    QCOMPARE(parser.intData(), 6512);
    type = parser.next();
    QCOMPARE(type, EndOfDocument);
}

void TestBuffers::testCMFBasic2()
{
    MessageBuilder builder(NoHeader);
    builder.add(129, 6512);
    ConstBuffer buf = builder.buffer();
    QCOMPARE(buf.size(), 5);
    QCOMPARE(static_cast<unsigned char>(buf[0]), 248);
    QCOMPARE(static_cast<unsigned char>(buf[1]), 128);
    QCOMPARE(buf[2], 1);
    QCOMPARE(static_cast<unsigned char>(buf[3]), 177);
    QCOMPARE(buf[4], 112);

    MessageParser parser(buf);
    ParsedType type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), 129);
    QCOMPARE(parser.intData(), 6512);
    type = parser.next();
    QCOMPARE(type, EndOfDocument);
}

void TestBuffers::testCMFTypes()
{
    MessageBuilder builder(NoHeader);
    builder.add(1, std::string("Föo"));
    std::vector<char> blob;
    blob.assign(4, 'h');
    blob[1] = blob[3] = 'i';
    builder.add(200, blob);
    builder.add(3, true);
    builder.add(40, false);

    ConstBuffer buf = builder.buffer();
    QCOMPARE(buf.size(), 17);

    // string '1'
    QCOMPARE(static_cast<unsigned char>(buf[0]), 10);
    QCOMPARE(static_cast<unsigned char>(buf[1]), 4); // serialized string length
    QCOMPARE(static_cast<unsigned char>(buf[2]), 70);
    QCOMPARE(static_cast<unsigned char>(buf[3]), 195);
    QCOMPARE(static_cast<unsigned char>(buf[4]), 182);
    QCOMPARE(static_cast<unsigned char>(buf[5]), 111);

    // blob '200'
    QCOMPARE(static_cast<unsigned char>(buf[6]), 251);
    QCOMPARE(static_cast<unsigned char>(buf[7]), 128);
    QCOMPARE(static_cast<unsigned char>(buf[8]), 72);
    QCOMPARE(static_cast<unsigned char>(buf[9]), 4); // length of bytearray
    QCOMPARE(static_cast<unsigned char>(buf[10]), 104);  //'h'
    QCOMPARE(static_cast<unsigned char>(buf[11]), 105);  //'i'
    QCOMPARE(static_cast<unsigned char>(buf[12]), 104);  //'h'
    QCOMPARE(static_cast<unsigned char>(buf[13]), 105);  //'i'

    // bool-true '3'
    QVERIFY(static_cast<unsigned char>(buf[14]) == 28);

    // bool-false '40'
    QVERIFY(static_cast<unsigned char>(buf[15]) == 253);
    QVERIFY(static_cast<unsigned char>(buf[16]) == 40);

    MessageParser parser(buf);
    QCOMPARE(parser.next(), FoundTag);
    QCOMPARE(parser.tag(), (unsigned int) 1);
    QCOMPARE(parser.stringData(), std::string("Föo"));
    QCOMPARE(parser.next(), FoundTag);
    QCOMPARE(parser.tag(), (unsigned int) 200);
    std::vector<char> blobCopy = parser.bytesData();
    QCOMPARE(blobCopy.size(), blob.size());
    for (unsigned int i = 0; i < blobCopy.size(); ++i) {
        QCOMPARE(blobCopy[i], blob[i]);
    }
    QCOMPARE(parser.next(), FoundTag);
    QCOMPARE(parser.tag(), (unsigned int) 3);
    QCOMPARE(parser.boolData(), true);
    QCOMPARE(parser.next(), FoundTag);
    QCOMPARE(parser.tag(), (unsigned int) 40);
    QCOMPARE(parser.boolData(), false);
    QCOMPARE(parser.next(), EndOfDocument);
}

void TestBuffers::testParsers()
{
    MessageBuilder builder(NoHeader);
    builder.add(1, 1);
    builder.add(2, -1);
    builder.add(3, 0);
    builder.add(4, (uint64_t) LONG_LONG_MAX);
    builder.add(5, (int) INT_MIN);
    builder.add(6, (uint64_t) ULONG_LONG_MAX);

    ConstBuffer buf = builder.buffer();
    QCOMPARE(buf.size(), 33);

    QCOMPARE(static_cast<unsigned char>(buf[0]), 8);
    QCOMPARE(static_cast<unsigned char>(buf[1]), 1);

    MessageParser parser(buf);
    auto type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), 1);
    QCOMPARE(parser.isInt(), true);
    QCOMPARE(parser.isLong(), true);
    QCOMPARE(parser.intData(), 1);
    QCOMPARE(parser.longData(), 1);

    QCOMPARE(static_cast<unsigned char>(buf[2]), 17);
    QCOMPARE(static_cast<unsigned char>(buf[3]), 1);

    type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), 2);
    QCOMPARE(parser.isInt(), true);
    QCOMPARE(parser.isLong(), true);
    QCOMPARE(parser.intData(), -1);
    QCOMPARE(parser.longData(), -1);

    QCOMPARE(static_cast<unsigned char>(buf[4]), 24);
    QCOMPARE(static_cast<unsigned char>(buf[5]), 0);

    type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), 3);
    QCOMPARE(parser.isInt(), true);
    QCOMPARE(parser.isLong(), true);
    QCOMPARE(parser.intData(), 0);
    QCOMPARE(parser.longData(), 0);

    QCOMPARE(static_cast<unsigned char>(buf[6]), 32);
    QCOMPARE(static_cast<unsigned char>(buf[7]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[8]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[9]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[10]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[11]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[12]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[13]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[14]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[15]), 0x7f);

    type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), 4);
    QCOMPARE(parser.isInt(), false);
    QCOMPARE(parser.isLong(), true);
    QCOMPARE(parser.longData(), LONG_LONG_MAX);

    QCOMPARE(static_cast<unsigned char>(buf[16]), 41);
    QCOMPARE(static_cast<unsigned char>(buf[17]), 0x86);
    QCOMPARE(static_cast<unsigned char>(buf[18]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[19]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[20]), 0xff);
    QCOMPARE(static_cast<unsigned char>(buf[21]), 0x0);

    type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), 5);
    QCOMPARE(parser.isInt(), true);
    QCOMPARE(parser.isLong(), true);
    QCOMPARE(parser.intData(), INT_MIN);


    QCOMPARE(static_cast<unsigned char>(buf[22]), 48);
    QCOMPARE(static_cast<unsigned char>(buf[23]), 0x80);
    QCOMPARE(static_cast<unsigned char>(buf[24]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[25]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[26]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[27]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[28]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[29]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[30]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[31]), 0xfe);
    QCOMPARE(static_cast<unsigned char>(buf[32]), 0x7f);

    type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), 6);
    QCOMPARE(parser.isInt(), false);
    QCOMPARE(parser.isLong(), true);
    QCOMPARE(parser.longData(), ULONG_LONG_MAX);

    type = parser.next();
    QCOMPARE(type, EndOfDocument);
}

void TestBuffers::benchSerialize()
{
    char buf[10];
    int bytes = Streaming::Private::serialize(buf, 992230948217398);
    QCOMPARE(bytes, 8);

    QBENCHMARK {
        int pos = 0;
        uint64_t result = 0;
        Streaming::Private::unserialize(buf, 10, pos, result);
        // QCOMPARE(result, 992230948217398);
    }
}

QTEST_MAIN(TestBuffers)
