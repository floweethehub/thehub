/*
 * This file is part of the Flowee project
 * Copyright (C) 2013-2015 The Bitcoin Core developers
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

#include "hash.h"
#include "test/test_bitcoin.h"
#include <utilstrencodings.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(hash_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(murmurhash3)
{

#define T(expected, seed, data) BOOST_CHECK_EQUAL(MurmurHash3(seed, ParseHex(data)), expected)

    // Test MurmurHash3 with various inputs. Of course this is retested in the
    // bloom filter tests - they would fail if MurmurHash3() had any problems -
    // but is useful for those trying to implement Bitcoin libraries as a
    // source of test data for their MurmurHash3() primitive during
    // development.
    //
    // The magic number 0xFBA4C795 comes from CBloomFilter::Hash()

    T(0x00000000, 0x00000000, "");
    T(0x6a396f08, 0xFBA4C795, "");
    T(0x81f16f39, 0xffffffff, "");

    T(0x514e28b7, 0x00000000, "00");
    T(0xea3f0b17, 0xFBA4C795, "00");
    T(0xfd6cf10d, 0x00000000, "ff");

    T(0x16c6b7ab, 0x00000000, "0011");
    T(0x8eb51c3d, 0x00000000, "001122");
    T(0xb4471bf8, 0x00000000, "00112233");
    T(0xe2301fa8, 0x00000000, "0011223344");
    T(0xfc2e4a15, 0x00000000, "001122334455");
    T(0xb074502c, 0x00000000, "00112233445566");
    T(0x8034d2a0, 0x00000000, "0011223344556677");
    T(0xb4698def, 0x00000000, "001122334455667788");

#undef T
}

BOOST_AUTO_TEST_SUITE_END()
