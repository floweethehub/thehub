/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
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
#include "test_hashstorage.h"

#include "../../indexer/HashStorage.h"

#include <utiltime.h>

void TestHashStorage::init()
{
    m_testPath = strprintf("test_flowee_%lu", (unsigned long)GetTime());
    boost::filesystem::remove_all(m_testPath);
    boost::filesystem::create_directories(m_testPath);
}

void TestHashStorage::cleanup()
{
    boost::filesystem::remove_all(m_testPath);
}

void TestHashStorage::basic()
{
    uint256 hash1 = uint256S("00001e397a22a7262ae899550d85ae9cb4ac314510d55d32a31cf86a792ea7ea");
    uint256 hash2 = uint256S("5123d8a19c8815f9395cd63abc796289ee7900135ed1b6674f76e7b5038e9a1d");
    uint256 hash3 = uint256S("00001e397a22a7262ae899450d85ae9cb4ac314510d55d32a31cf86a792ea6ea");
    HashIndexPoint index1, index2, index3;
    {
        HashStorage hs(m_testPath);
        index1 = hs.append(hash1);
        index2 = hs.append(hash2);
        QCOMPARE(index1.db, index2.db);
        QVERIFY(index1.row != index2.row);
    }
    {
        HashStorage hs(m_testPath);
        QCOMPARE(hash1, hs.at(index1));
        QCOMPARE(hash2, hs.at(index2));
        index3 = hs.append(hash3);
        QCOMPARE(hash1, hs.at(index1));
        QCOMPARE(hash2, hs.at(index2));
        QCOMPARE(hash3, hs.at(index3));
    }
    {
        HashStorage hs(m_testPath);
        QCOMPARE(hash1, hs.at(index1));
        QCOMPARE(hash2, hs.at(index2));
        QCOMPARE(hash3, hs.at(index3));

        QCOMPARE(hs.find(hash1), index1);

        hs.finalize();

        QCOMPARE(hash1, hs.at(index1));
        QCOMPARE(hash2, hs.at(index2));
        QCOMPARE(hash3, hs.at(index3));

        QCOMPARE(hs.find(hash1), index1);
    }

    uint256 hash4 = uint256S("0e3e2357e806b6cdb1f70b54c3a3a17b6714ee1f0e68bebb44a74b1efd512098");
    HashIndexPoint index4;
    {
        HashStorage hs(m_testPath);
        QCOMPARE(hash1, hs.at(index1));
        QCOMPARE(hash2, hs.at(index2));
        QCOMPARE(hash3, hs.at(index3));
        QCOMPARE(hs.find(hash1), index1);

        // insert more and finalize again.
        index4 = hs.append(hash4);
        QCOMPARE(hash1, hs.at(index1));
        QCOMPARE(hash2, hs.at(index2));
        QCOMPARE(hash3, hs.at(index3));
        QCOMPARE(hash4, hs.at(index4));
        QCOMPARE(hs.find(hash2), index2);

        // second round.
        hs.finalize();
        QCOMPARE(hash1, hs.at(index1));
        QCOMPARE(hash2, hs.at(index2));
        QCOMPARE(hash3, hs.at(index3));
        QCOMPARE(hash4, hs.at(index4));
        QCOMPARE(hs.find(hash2), index2);
    }

    {
        HashStorage hs(m_testPath);
        QCOMPARE(hash1, hs.at(index1));
        QCOMPARE(hash2, hs.at(index2));
        QCOMPARE(hash3, hs.at(index3));
        QCOMPARE(hash4, hs.at(index4));
        QCOMPARE(hs.find(hash2), index2);
    }
}

QTEST_MAIN(TestHashStorage)
