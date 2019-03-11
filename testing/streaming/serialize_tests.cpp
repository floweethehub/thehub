/*
 * This file is part of the Flowee project
 * Copyright (C) 2012-2015 The Bitcoin Core developers
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

#include "serialize_tests.h"
#include <serialize.h>
#include <hash.h>
#include <streaming/streams.h>

void Test_Serialize::sizes()
{
    QCOMPARE(sizeof(char), (size_t) GetSerializeSize(char(0), 0));
    QCOMPARE(sizeof(int8_t), (size_t) GetSerializeSize(int8_t(0), 0));
    QCOMPARE(sizeof(uint8_t), (size_t) GetSerializeSize(uint8_t(0), 0));
    QCOMPARE(sizeof(int16_t), (size_t) GetSerializeSize(int16_t(0), 0));
    QCOMPARE(sizeof(uint16_t), (size_t) GetSerializeSize(uint16_t(0), 0));
    QCOMPARE(sizeof(int32_t), (size_t) GetSerializeSize(int32_t(0), 0));
    QCOMPARE(sizeof(uint32_t), (size_t) GetSerializeSize(uint32_t(0), 0));
    QCOMPARE(sizeof(int64_t), (size_t) GetSerializeSize(int64_t(0), 0));
    QCOMPARE(sizeof(uint64_t), (size_t) GetSerializeSize(uint64_t(0), 0));
    QCOMPARE(sizeof(float), (size_t) GetSerializeSize(float(0), 0));
    QCOMPARE(sizeof(double), (size_t) GetSerializeSize(double(0), 0));
    // Bool is serialized as char
    QCOMPARE(sizeof(char), (size_t) GetSerializeSize(bool(0), 0));

    // Sanity-check GetSerializeSize and c++ type matching
    QCOMPARE((int) GetSerializeSize(char(0), 0), 1);
    QCOMPARE((int) GetSerializeSize(int8_t(0), 0), 1);
    QCOMPARE((int) GetSerializeSize(uint8_t(0), 0), 1);
    QCOMPARE((int) GetSerializeSize(int16_t(0), 0), 2);
    QCOMPARE((int) GetSerializeSize(uint16_t(0), 0), 2);
    QCOMPARE((int) GetSerializeSize(int32_t(0), 0), 4);
    QCOMPARE((int) GetSerializeSize(uint32_t(0), 0), 4);
    QCOMPARE((int) GetSerializeSize(int64_t(0), 0), 8);
    QCOMPARE((int) GetSerializeSize(uint64_t(0), 0), 8);
    QCOMPARE((int) GetSerializeSize(float(0), 0), 4);
    QCOMPARE((int) GetSerializeSize(double(0), 0), 8);
    QCOMPARE((int) GetSerializeSize(bool(0), 0), 1);
}

void Test_Serialize::floats_conversion()
{
    // Choose values that map unambigiously to binary floating point to avoid
    // rounding issues at the compiler side.
    QCOMPARE(ser_uint32_to_float(0x00000000), 0.0F);
    QCOMPARE(ser_uint32_to_float(0x3f000000), 0.5F);
    QCOMPARE(ser_uint32_to_float(0x3f800000), 1.0F);
    QCOMPARE(ser_uint32_to_float(0x40000000), 2.0F);
    QCOMPARE(ser_uint32_to_float(0x40800000), 4.0F);
    QCOMPARE(ser_uint32_to_float(0x44444444), 785.066650390625F);

    QCOMPARE(ser_float_to_uint32(0.0F), (uint) 0x00000000);
    QCOMPARE(ser_float_to_uint32(0.5F), (uint) 0x3f000000);
    QCOMPARE(ser_float_to_uint32(1.0F), (uint) 0x3f800000);
    QCOMPARE(ser_float_to_uint32(2.0F), (uint) 0x40000000);
    QCOMPARE(ser_float_to_uint32(4.0F), (uint) 0x40800000);
    QCOMPARE(ser_float_to_uint32(785.066650390625F), (uint) 0x44444444);
}

void Test_Serialize::doubles_conversion()
{
    // Choose values that map unambigiously to binary floating point to avoid
    // rounding issues at the compiler side.
    QCOMPARE(ser_uint64_to_double(0x0000000000000000ULL), 0.0);
    QCOMPARE(ser_uint64_to_double(0x3fe0000000000000ULL), 0.5);
    QCOMPARE(ser_uint64_to_double(0x3ff0000000000000ULL), 1.0);
    QCOMPARE(ser_uint64_to_double(0x4000000000000000ULL), 2.0);
    QCOMPARE(ser_uint64_to_double(0x4010000000000000ULL), 4.0);
    QCOMPARE(ser_uint64_to_double(0x4088888880000000ULL), 785.066650390625);

    QCOMPARE(ser_double_to_uint64(0.0), 0x0000000000000000UL);
    QCOMPARE(ser_double_to_uint64(0.5), 0x3fe0000000000000UL);
    QCOMPARE(ser_double_to_uint64(1.0), 0x3ff0000000000000UL);
    QCOMPARE(ser_double_to_uint64(2.0), 0x4000000000000000UL);
    QCOMPARE(ser_double_to_uint64(4.0), 0x4010000000000000UL);
    QCOMPARE(ser_double_to_uint64(785.066650390625), 0x4088888880000000UL);
}
/*
Python code to generate the below hashes:

    def reversed_hex(x):
        return binascii.hexlify(''.join(reversed(x)))
    def dsha256(x):
        return hashlib.sha256(hashlib.sha256(x).digest()).digest()

    reversed_hex(dsha256(''.join(struct.pack('<f', x) for x in range(0,1000)))) == '8e8b4cf3e4df8b332057e3e23af42ebc663b61e0495d5e7e32d85099d7f3fe0c'
    reversed_hex(dsha256(''.join(struct.pack('<d', x) for x in range(0,1000)))) == '43d0c82591953c4eafe114590d392676a01585d25b25d433557f0d7878b23f96'
*/
void Test_Serialize::floats()
{
    CDataStream ss(SER_DISK, 0);
    // encode
    for (int i = 0; i < 1000; i++) {
        ss << float(i);
    }
    QVERIFY(Hash(ss.begin(), ss.end()) == uint256S("8e8b4cf3e4df8b332057e3e23af42ebc663b61e0495d5e7e32d85099d7f3fe0c"));

    // decode
    for (int i = 0; i < 1000; i++) {
        float j;
        ss >> j;
        QCOMPARE((int) j, i);
    }
}

void Test_Serialize::doubles()
{
    CDataStream ss(SER_DISK, 0);
    // encode
    for (int i = 0; i < 1000; i++) {
        ss << double(i);
    }
    QVERIFY(Hash(ss.begin(), ss.end()) == uint256S("43d0c82591953c4eafe114590d392676a01585d25b25d433557f0d7878b23f96"));

    // decode
    for (int i = 0; i < 1000; i++) {
        double j;
        ss >> j;
        QCOMPARE((int) j, i);
    }
}

void Test_Serialize::varints()
{
    // encode

    CDataStream ss(SER_DISK, 0);
    CDataStream::size_type size = 0;
    for (int i = 0; i < 100000; i++) {
        ss << VARINT(i);
        size += ::GetSerializeSize(VARINT(i), 0, 0);
        QVERIFY(size == ss.size());
    }

    for (uint64_t i = 0;  i < 100000000000ULL; i += 999999937) {
        ss << VARINT(i);
        size += ::GetSerializeSize(VARINT(i), 0, 0);
        QVERIFY(size == ss.size());
    }

    // decode
    for (int i = 0; i < 100000; i++) {
        int j = -1;
        ss >> VARINT(j);
        QCOMPARE(j, i);
    }

    for (uint64_t i = 0;  i < 100000000000ULL; i += 999999937) {
        uint64_t j = -1;
        ss >> VARINT(j);
        QCOMPARE(j, i);
    }
}

void Test_Serialize::compactsize()
{
    CDataStream ss(SER_DISK, 0);
    std::vector<char>::size_type i, j;

    for (i = 1; i <= MAX_SIZE; i *= 2)
    {
        WriteCompactSize(ss, i-1);
        WriteCompactSize(ss, i);
    }
    for (i = 1; i <= MAX_SIZE; i *= 2)
    {
        j = ReadCompactSize(ss);
        QCOMPARE(j, (i-1));
        j = ReadCompactSize(ss);
        QCOMPARE(j, i);
    }
}

static bool isCanonicalException(const std::ios_base::failure& ex)
{
    std::ios_base::failure expectedException("non-canonical ReadCompactSize()");

    // The string returned by what() can be different for different platforms.
    // Instead of directly comparing the ex.what() with an expected string,
    // create an instance of exception to see if ex.what() matches 
    // the expected explanatory string returned by the exception instance. 
    return strcmp(expectedException.what(), ex.what()) == 0;
}


void Test_Serialize::noncanonical()
{
    // Write some non-canonical CompactSize encodings, and
    // make sure an exception is thrown when read back.
    CDataStream ss(SER_DISK, 0);
    std::vector<char>::size_type n;

    // zero encoded with three bytes:
    ss.write("\xfd\x00\x00", 3);
    try {
        ReadCompactSize(ss);
        QVERIFY(false);
    } catch (std::ios_base::failure &e) {
        QVERIFY(isCanonicalException(e));
    } catch (...) { QVERIFY(false); }

    // 0xfc encoded with three bytes:
    ss.write("\xfd\xfc\x00", 3);
    try {
        ReadCompactSize(ss);
        QVERIFY(false);
    } catch (std::ios_base::failure &e) {
        QVERIFY(isCanonicalException(e));
    } catch (...) { QVERIFY(false); }

    // 0xfd encoded with three bytes is OK:
    ss.write("\xfd\xfd\x00", 3);
    n = ReadCompactSize(ss);
    QVERIFY(n == 0xfd);

    // zero encoded with five bytes:
    ss.write("\xfe\x00\x00\x00\x00", 5);
    try {
        ReadCompactSize(ss);
        QVERIFY(false);
    } catch (std::ios_base::failure &e) {
        QVERIFY(isCanonicalException(e));
    } catch (...) { QVERIFY(false); }

    // 0xffff encoded with five bytes:
    ss.write("\xfe\xff\xff\x00\x00", 5);
    try {
        ReadCompactSize(ss);
        QVERIFY(false);
    } catch (std::ios_base::failure &e) {
        QVERIFY(isCanonicalException(e));
    } catch (...) { QVERIFY(false); }

    // zero encoded with nine bytes:
    ss.write("\xff\x00\x00\x00\x00\x00\x00\x00\x00", 9);
    try {
        ReadCompactSize(ss);
        QVERIFY(false);
    } catch (std::ios_base::failure &e) {
        QVERIFY(isCanonicalException(e));
    } catch (...) { QVERIFY(false); }

    // 0x01ffffff encoded with nine bytes:
    ss.write("\xff\xff\xff\xff\x01\x00\x00\x00\x00", 9);
    try {
        ReadCompactSize(ss);
        QVERIFY(false);
    } catch (std::ios_base::failure &e) {
        QVERIFY(isCanonicalException(e));
    } catch (...) { QVERIFY(false); }
}

void Test_Serialize::insert_delete()
{
    // Test inserting/deleting bytes.
    CDataStream ss(SER_DISK, 0);
    QCOMPARE(ss.size(), (size_t) 0);

    ss.write("\x00\x01\x02\xff", 4);
    QCOMPARE(ss.size(), (size_t) 4);

    char c = (char)11;

    // Inserting at beginning/end/middle:
    ss.insert(ss.begin(), c);
    QCOMPARE(ss.size(), (size_t) 5);
    QCOMPARE(ss[0], c);
    QCOMPARE(ss[1], '\0');

    ss.insert(ss.end(), c);
    QCOMPARE(ss.size(), (size_t) 6);
    QCOMPARE(ss[4], (char)0xff);
    QCOMPARE(ss[5], c);

    ss.insert(ss.begin()+2, c);
    QCOMPARE(ss.size(), (size_t) 7);
    QCOMPARE(ss[2], c);

    // Delete at beginning/end/middle
    ss.erase(ss.begin());
    QCOMPARE(ss.size(), (size_t) 6);
    QCOMPARE(ss[0], '\0');

    ss.erase(ss.begin()+ss.size()-1);
    QCOMPARE(ss.size(), (size_t) 5);
    QCOMPARE(ss[4], (char)0xff);

    ss.erase(ss.begin()+1);
    QCOMPARE(ss.size(), (size_t) 4);
    QCOMPARE(ss[0], '\0');
    QCOMPARE(ss[1], '\1');
    QCOMPARE(ss[2], '\2');
    QCOMPARE(ss[3], (char)0xff);

    // Make sure GetAndClear does the right thing:
    CSerializeData d;
    ss.GetAndClear(d);
    QCOMPARE(ss.size(), (size_t) 0);
}
