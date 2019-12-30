/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2019 Tom Zander <tomz@freedommail.ch>
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

void TestBuffers::testBuilderReply()
{
    Message input(4, 101);
    input.setHeaderInt(11, 21);
    input.setHeaderInt(110, 91);

    MessageBuilder builder(NoHeader);
    builder.add(1, "bla");
    Message reply = builder.reply(input);
    QVERIFY(reply.body().size() == 5);
    QCOMPARE(reply.headerInt(Network::ServiceId), 4);
    QCOMPARE(reply.headerInt(Network::MessageId), 102); // input + 1
    QCOMPARE(reply.headerInt(11), 21);
    QCOMPARE(reply.headerInt(110), 91);
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
    QCOMPARE(data[0], (char) 120);
    QCOMPARE((unsigned char)data[1], (unsigned char) 177);
    QCOMPARE(data[2], (char) 112);

    MessageParser parser(buf);
    ParsedType type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), (uint32_t) 15);
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
    QCOMPARE((unsigned char)buf[0], (unsigned char) 248);
    QCOMPARE((unsigned char)buf[1], (unsigned char) 128);
    QCOMPARE(buf[2], (char) 1);
    QCOMPARE((unsigned char)buf[3], (unsigned char) 177);
    QCOMPARE(buf[4], (char) 112);

    MessageParser parser(buf);
    ParsedType type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), (uint32_t) 129);
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
    QCOMPARE((uint8_t)buf[0], (uint8_t)10);
    QCOMPARE((uint8_t)buf[1], (uint8_t)4); // serialized string length
    QCOMPARE((uint8_t)buf[2], (uint8_t)70);
    QCOMPARE((uint8_t)buf[3], (uint8_t)195);
    QCOMPARE((uint8_t)buf[4], (uint8_t)182);
    QCOMPARE((uint8_t)buf[5], (uint8_t)111);

    // blob '200'
    QCOMPARE((uint8_t)buf[6], (uint8_t) 251);
    QCOMPARE((uint8_t)buf[7], (uint8_t) 128);
    QCOMPARE((uint8_t)buf[8], (uint8_t)72);
    QCOMPARE((uint8_t)buf[9], (uint8_t)4); // length of bytearray
    QCOMPARE((uint8_t)buf[10], (uint8_t)104);  //'h'
    QCOMPARE((uint8_t)buf[11], (uint8_t)105);  //'i'
    QCOMPARE((uint8_t)buf[12], (uint8_t)104);  //'h'
    QCOMPARE((uint8_t)buf[13], (uint8_t)105);  //'i'

    // bool-true '3'
    QVERIFY((unsigned char)buf[14] == 28);

    // bool-false '40'
    QVERIFY((uint8_t)buf[15] == (unsigned char)253);
    QVERIFY((uint8_t)buf[16] == 40);

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

    QCOMPARE((uint8_t)buf[0], (uint8_t)8);
    QCOMPARE((uint8_t)buf[1], (uint8_t)1);

    MessageParser parser(buf);
    auto type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), (uint32_t) 1);
    QCOMPARE(parser.isInt(), true);
    QCOMPARE(parser.isLong(), true);
    QCOMPARE(parser.intData(), 1);
    QCOMPARE(parser.longData(), (uint64_t) 1);

    QCOMPARE((uint8_t)buf[2], (uint8_t)17);
    QCOMPARE((uint8_t)buf[3], (uint8_t)1);

    type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), (uint32_t) 2);
    QCOMPARE(parser.isInt(), true);
    QCOMPARE(parser.isLong(), true);
    QCOMPARE(parser.intData(), -1);
    QCOMPARE(parser.longData(), (uint64_t)-1);

    QCOMPARE((uint8_t)buf[4], (uint8_t)24);
    QCOMPARE((uint8_t)buf[5], (uint8_t)0);

    type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), (uint32_t) 3);
    QCOMPARE(parser.isInt(), true);
    QCOMPARE(parser.isLong(), true);
    QCOMPARE(parser.intData(), 0);
    QCOMPARE(parser.longData(), (uint64_t) 0);

    QCOMPARE((uint8_t)buf[6], (uint8_t)32);
    QCOMPARE((uint8_t)buf[7], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[8], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[9], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[10], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[11], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[12], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[13], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[14], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[15], (uint8_t)0x7f);

    type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), (uint32_t) 4);
    QCOMPARE(parser.isInt(), false);
    QCOMPARE(parser.isLong(), true);
    QCOMPARE(parser.longData(), (uint64_t) LONG_LONG_MAX);

    QCOMPARE((uint8_t)buf[16], (uint8_t)41);
    QCOMPARE((uint8_t)buf[17], (uint8_t)0x86);
    QCOMPARE((uint8_t)buf[18], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[19], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[20], (uint8_t)0xff);
    QCOMPARE((uint8_t)buf[21], (uint8_t)0);

    type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), (uint32_t) 5);
    QCOMPARE(parser.isInt(), true);
    QCOMPARE(parser.isLong(), true);
    QCOMPARE(parser.intData(), INT_MIN);

    QCOMPARE((uint8_t)buf[22], (uint8_t)48);
    QCOMPARE((uint8_t)buf[23], (uint8_t)0x80);
    QCOMPARE((uint8_t)buf[24], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[25], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[26], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[27], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[28], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[29], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[30], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[31], (uint8_t)0xfe);
    QCOMPARE((uint8_t)buf[32], (uint8_t)0x7f);

    type = parser.next();
    QCOMPARE(type, FoundTag);
    QCOMPARE(parser.tag(), (uint32_t) 6);
    QCOMPARE(parser.isInt(), false);
    QCOMPARE(parser.isLong(), true);
    QCOMPARE(parser.longData(), (uint64_t) ULONG_LONG_MAX);

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

void TestBuffers::testCompare()
{
    // test the operator== method

    Streaming::BufferPool pool;
    pool.writeHex("0x308400123809128309182093801923809128309128");
    Streaming::ConstBuffer buf = pool.commit();

    QCOMPARE(buf == buf, true);
    QCOMPARE(buf == Streaming::ConstBuffer(), false);
    QCOMPARE(Streaming::ConstBuffer() == buf, false);
    QCOMPARE(Streaming::ConstBuffer() == Streaming::ConstBuffer(), true);
    QCOMPARE(buf == buf.mid(1), false);
    QCOMPARE(buf == buf.mid(0, 10), false);

    auto x = buf;
    QCOMPARE(buf == x, true);


    pool.writeHex("0x308400123809128309182093801923809128309128");
    Streaming::ConstBuffer buf2 = pool.commit();
    QCOMPARE(buf == buf2, true);
}

void TestBuffers::testConstBufMid()
{
    Streaming::BufferPool pool;
    pool.writeHex("0x308409123809128309182093801923809128309128");
    Streaming::ConstBuffer buf = pool.commit();
    QCOMPARE(buf.size(), 21);
    QCOMPARE(buf.isEmpty(), false);
    QCOMPARE(buf.isValid(), true);

    auto buf2 = buf.mid(4, 5);
    QCOMPARE(buf2.size(), 5);
    QCOMPARE(buf2.begin(), buf.begin() + 4);

    buf2 = buf.mid(6);
    QCOMPARE(buf2.size(), 21 - 6);
    QCOMPARE(buf2.begin(), buf.begin() + 6);
}

void TestBuffers::testConstBufStartsWith()
{
    Streaming::BufferPool pool;
    pool.writeHex("0x308409123809128309182093801923809128309128");
    Streaming::ConstBuffer buf = pool.commit();
    QCOMPARE(buf.size(), 21);
    auto buf2 = buf.mid(0, 10);
    QVERIFY(buf.startsWith(buf2));
    QVERIFY(buf2.startsWith(buf2));
    QVERIFY(!buf2.startsWith(buf));
    QVERIFY(!buf2.startsWith(Streaming::ConstBuffer()));
    QVERIFY(!buf2.startsWith(buf.mid(1)));
}
