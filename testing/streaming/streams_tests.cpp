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

#include <streams.h>

#include <vector>
#include <boost/assign/std/vector.hpp> // for 'operator+=()'

using namespace boost::assign; // bring 'operator+=()' into scope

void TestXor::streams_serializedata_xor()
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
