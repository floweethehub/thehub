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

#include "streams_tests.h"

#include <streaming/streams.h>
#include <utilstrencodings.h>

#include <vector>
#include <boost/assign/std/vector.hpp> // for 'operator+=()'

using namespace boost::assign; // bring 'operator+=()' into scope

void TestStreams::streams_serializedata_xor()
{
    std::vector<char> in;
    std::vector<char> expected_xor;
    std::vector<unsigned char> key;
    CDataStream ds(in, 0, 0);

    // Degenerate case
    key += '\x00','\x00';
    ds.Xor(key);
    QCOMPARE(
            std::string(expected_xor.begin(), expected_xor.end()),
            std::string(ds.begin(), ds.end()));

    in += '\x0f','\xf0';
    expected_xor += '\xf0','\x0f';

    // Single character key

    ds.clear();
    ds.insert(ds.begin(), in.begin(), in.end());
    key.clear();

    key += '\xff';
    ds.Xor(key);
    QCOMPARE(
            std::string(expected_xor.begin(), expected_xor.end()),
            std::string(ds.begin(), ds.end()));

    // Multi character key

    in.clear();
    expected_xor.clear();
    in += '\xf0','\x0f';
    expected_xor += '\x0f','\x00';

    ds.clear();
    ds.insert(ds.begin(), in.begin(), in.end());

    key.clear();
    key += '\xff','\x0f';

    ds.Xor(key);
    QCOMPARE(
            std::string(expected_xor.begin(), expected_xor.end()),
            std::string(ds.begin(), ds.end()));
}

#define B "check_prefix"
#define E "check_postfix"
/* Test strprintf formatting directives.
 * Put a string before and after to ensure sanity of element sizes on stack. */
void TestStreams::testStrPrintf()
{
    int64_t s64t = -9223372036854775807LL; /* signed 64 bit test value */
    uint64_t u64t = 18446744073709551615ULL; /* unsigned 64 bit test value */
    QVERIFY(strprintf("%s %d %s", B, s64t, E) == B" -9223372036854775807 " E);
    QVERIFY(strprintf("%s %u %s", B, u64t, E) == B" 18446744073709551615 " E);
    QVERIFY(strprintf("%s %x %s", B, u64t, E) == B" ffffffffffffffff " E);

    size_t st = 12345678; /* unsigned size_t test value */
    ssize_t sst = -12345678; /* signed size_t test value */
    QVERIFY(strprintf("%s %d %s", B, sst, E) == B" -12345678 " E);
    QVERIFY(strprintf("%s %u %s", B, st, E) == B" 12345678 " E);
    QVERIFY(strprintf("%s %x %s", B, st, E) == B" bc614e " E);

    ptrdiff_t pt = 87654321; /* positive ptrdiff_t test value */
    ptrdiff_t spt = -87654321; /* negative ptrdiff_t test value */
    QVERIFY(strprintf("%s %d %s", B, spt, E) == B" -87654321 " E);
    QVERIFY(strprintf("%s %u %s", B, pt, E) == B" 87654321 " E);
    QVERIFY(strprintf("%s %x %s", B, pt, E) == B" 5397fb1 " E);
}
#undef B
#undef E

void TestStreams::testParseInt32()
{
    int32_t n;
    // Valid values
    QVERIFY(ParseInt32("1234", NULL));
    QVERIFY(ParseInt32("0", &n) && n == 0);
    QVERIFY(ParseInt32("1234", &n) && n == 1234);
    QVERIFY(ParseInt32("01234", &n) && n == 1234); // no octal
    QVERIFY(ParseInt32("2147483647", &n) && n == 2147483647);
    QVERIFY(ParseInt32("-2147483648", &n) && n == -2147483648);
    QVERIFY(ParseInt32("-1234", &n) && n == -1234);
    // Invalid values
    QVERIFY(!ParseInt32("", &n));
    QVERIFY(!ParseInt32(" 1", &n)); // no padding inside
    QVERIFY(!ParseInt32("1 ", &n));
    QVERIFY(!ParseInt32("1a", &n));
    QVERIFY(!ParseInt32("aap", &n));
    QVERIFY(!ParseInt32("0x1", &n)); // no hex
    QVERIFY(!ParseInt32("0x1", &n)); // no hex
    const char test_bytes[] = {'1', 0, '1'};
    std::string teststr(test_bytes, sizeof(test_bytes));
    QVERIFY(!ParseInt32(teststr, &n)); // no embedded NULs
    // Overflow and underflow
    QVERIFY(!ParseInt32("-2147483649", NULL));
    QVERIFY(!ParseInt32("2147483648", NULL));
    QVERIFY(!ParseInt32("-32482348723847471234", NULL));
    QVERIFY(!ParseInt32("32482348723847471234", NULL));
}

void TestStreams::testParseInt64()
{
    int64_t n;
    // Valid values
    QVERIFY(ParseInt64("1234", NULL));
    QVERIFY(ParseInt64("0", &n) && n == 0LL);
    QVERIFY(ParseInt64("1234", &n) && n == 1234LL);
    QVERIFY(ParseInt64("01234", &n) && n == 1234LL); // no octal
    QVERIFY(ParseInt64("2147483647", &n) && n == 2147483647LL);
    QVERIFY(ParseInt64("-2147483648", &n) && n == -2147483648LL);
    QVERIFY(ParseInt64("9223372036854775807", &n) && n == (int64_t)9223372036854775807);
    QVERIFY(ParseInt64("-9223372036854775808", &n) && n == (int64_t)-9223372036854775807-1);
    QVERIFY(ParseInt64("-1234", &n) && n == -1234LL);
    // Invalid values
    QVERIFY(!ParseInt64("", &n));
    QVERIFY(!ParseInt64(" 1", &n)); // no padding inside
    QVERIFY(!ParseInt64("1 ", &n));
    QVERIFY(!ParseInt64("1a", &n));
    QVERIFY(!ParseInt64("aap", &n));
    QVERIFY(!ParseInt64("0x1", &n)); // no hex
    const char test_bytes[] = {'1', 0, '1'};
    std::string teststr(test_bytes, sizeof(test_bytes));
    QVERIFY(!ParseInt64(teststr, &n)); // no embedded NULs
    // Overflow and underflow
    QVERIFY(!ParseInt64("-9223372036854775809", NULL));
    QVERIFY(!ParseInt64("9223372036854775808", NULL));
    QVERIFY(!ParseInt64("-32482348723847471234", NULL));
    QVERIFY(!ParseInt64("32482348723847471234", NULL));
}

void TestStreams::testParseDouble()
{
    double n;
    // Valid values
    QVERIFY(ParseDouble("1234", NULL));
    QVERIFY(ParseDouble("0", &n) && n == 0.0);
    QVERIFY(ParseDouble("1234", &n) && n == 1234.0);
    QVERIFY(ParseDouble("01234", &n) && n == 1234.0); // no octal
    QVERIFY(ParseDouble("2147483647", &n) && n == 2147483647.0);
    QVERIFY(ParseDouble("-2147483648", &n) && n == -2147483648.0);
    QVERIFY(ParseDouble("-1234", &n) && n == -1234.0);
    QVERIFY(ParseDouble("1e6", &n) && n == 1e6);
    QVERIFY(ParseDouble("-1e6", &n) && n == -1e6);
    // Invalid values
    QVERIFY(!ParseDouble("", &n));
    QVERIFY(!ParseDouble(" 1", &n)); // no padding inside
    QVERIFY(!ParseDouble("1 ", &n));
    QVERIFY(!ParseDouble("1a", &n));
    QVERIFY(!ParseDouble("aap", &n));
    QVERIFY(!ParseDouble("0x1", &n)); // no hex
    const char test_bytes[] = {'1', 0, '1'};
    std::string teststr(test_bytes, sizeof(test_bytes));
    QVERIFY(!ParseDouble(teststr, &n)); // no embedded NULs
    // Overflow and underflow
    QVERIFY(!ParseDouble("-1e10000", NULL));
    QVERIFY(!ParseDouble("1e10000", NULL));
}
