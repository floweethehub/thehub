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
#include "../../indexer/HashStorage_p.h"

#include <utiltime.h>

namespace {
inline uint160 uint160S(const char *str)
{
    uint160 rv;
    rv.SetHex(str);
    return rv;
}
}

class OpenHashStorage {
public:
    HashStoragePrivate *d;
};

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
    uint160 hash1 = uint160S("00001e397a22a7262ae899550d85ae9cb4ac3145");
    uint160 hash2 = uint160S("5123d8a19c8815f9395cd63abc796289ee790013");
    uint160 hash3 = uint160S("00001e397a22a7262ae899450d85ae9cb4ac3155");
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
        QCOMPARE(hash1, hs.find(index1));
        QCOMPARE(hash2, hs.find(index2));
        index3 = hs.append(hash3);
        QCOMPARE(hash1, hs.find(index1));
        QCOMPARE(hash2, hs.find(index2));
        QCOMPARE(hash3, hs.find(index3));
    }
    {
        HashStorage hs(m_testPath);
        QCOMPARE(hs.find(index1), hash1);
        QCOMPARE(hs.find(index2), hash2);
        QCOMPARE(hs.find(index3), hash3);

        QCOMPARE(index1, hs.lookup(hash1));

        hs.finalize();

        QCOMPARE(hs.find(index1), hash1);
        QCOMPARE(hs.find(index2), hash2);
        QCOMPARE(hs.find(index3), hash3);

        QCOMPARE(hs.lookup(hash1), index1);
    }

    uint160 hash4 = uint160S("0e3e2357e806b6cdb1f70b54c3a3a17b6714ee1f");
    HashIndexPoint index4;
    {
        HashStorage hs(m_testPath);
        QCOMPARE(hs.find(index1), hash1);
        QCOMPARE(hs.find(index2), hash2);
        QCOMPARE(hs.find(index3), hash3);
        QCOMPARE(hs.lookup(hash1), index1);

        // insert more and finalize again.
        index4 = hs.append(hash4);
        QCOMPARE(hs.find(index1), hash1);
        QCOMPARE(hs.find(index2), hash2);
        QCOMPARE(hs.find(index3), hash3);
        QCOMPARE(hs.find(index4), hash4);
        QCOMPARE(hs.lookup(hash3), index3);

        // second round.
        hs.finalize();
        QCOMPARE(hs.find(index1), hash1);
        QCOMPARE(hs.find(index2), hash2);
        QCOMPARE(hs.find(index3), hash3);
        QCOMPARE(hs.find(index4), hash4);
        QCOMPARE(hs.lookup(hash3), index3);
    }
    {
        HashStorage hs(m_testPath);
        QCOMPARE(hash1, hs.find(index1));
        QCOMPARE(hash2, hs.find(index2));
        QCOMPARE(hash3, hs.find(index3));
        QCOMPARE(hash4, hs.find(index4));
        QCOMPARE(hs.lookup(hash1), index1);
    }
}

void TestHashStorage::multipleDbs()
{
    HashStorage hs(m_testPath);
    HashStoragePrivate *d = reinterpret_cast<OpenHashStorage*>(&hs)->d;
    Q_ASSERT(d);
    QCOMPARE(d->dbs.length(), 1);
}

QTEST_MAIN(TestHashStorage)
