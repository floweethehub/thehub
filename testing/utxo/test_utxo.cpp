/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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
#include <utxo/UnspentOutputDatabase.h>
#include <server/chainparams.h>

#include <WorkerThreads.h>
#include <util.h>

#include <boost/test/unit_test.hpp>
#include <boost/test/included/unit_test_framework.hpp>
#include <boost/filesystem.hpp>

using boost::unit_test::test_suite;

class BasicFixture
{
public:
    void setup() {
        m_testPath = GetTempPath() / strprintf("test_flowee_%lu", (unsigned long)GetTime());
        boost::filesystem::remove_all(m_testPath);
    }
    void teardown() {
        boost::filesystem::remove_all(m_testPath);
    }
    void insertTransactions(UnspentOutputDatabase &db, int number) {
        for (int i = 0; i < number; ++i) {
            char buf[67];
            sprintf(buf, templateTxId, i);
            uint256 txid = uint256S(buf);
            db.insert(txid, 0, 6000+i, 100+i);
            db.insert(txid, 1, 6000+i, 100+i);

            UnspentOutput uo = db.find(txid, 0);
            BOOST_CHECK_EQUAL(uo.offsetInBlock(), 6000 + i);
            BOOST_CHECK_EQUAL(uo.blockHeight(), 100 + i);
        }
    }

    uint256 insertedTxId(int index) {
        char buf[67];
        sprintf(buf, templateTxId, index);
        return uint256S(buf);
    }


protected:
    boost::filesystem::path m_testPath;
    const char *templateTxId = "0x12345%02x17444b051c44dfd2720e88f314ff94f3dd6d56d40ef65854fcd7fff6b";
};


BOOST_FIXTURE_TEST_SUITE(utxo, BasicFixture)

BOOST_AUTO_TEST_CASE(basic)
{
    boost::asio::io_service ioService;
    UnspentOutputDatabase db(ioService, m_testPath);
    uint256 txid = uint256S("0xb4749f017444b051c44dfd2720e88f314ff94f3dd6d56d40ef65854fcd7fff6b");
    db.insert(txid, 0, 6000, 100);
    UnspentOutput uo = db.find(txid, 0);
    BOOST_CHECK_EQUAL(uo.offsetInBlock(), 6000);
    BOOST_CHECK_EQUAL(uo.blockHeight(), 100);

    bool success = db.remove(txid, 0);
    BOOST_CHECK(success);

    UnspentOutput uo2 = db.find(txid, 0);
    BOOST_CHECK_EQUAL(uo2.blockHeight(), 0);

    bool removed = db.remove(txid, 0);
    BOOST_CHECK_EQUAL(removed, false);
}

// test if we can keep multiple entries separate
BOOST_AUTO_TEST_CASE(multiple)
{
    boost::asio::io_service ioService;
    UnspentOutputDatabase db(ioService, m_testPath);

    insertTransactions(db, 100);
    const uint256 remove1 = insertedTxId(20);
    const uint256 remove2 = insertedTxId(89);
    bool success = db.remove(remove1, 0);
    BOOST_CHECK(success);

    UnspentOutput find1 = db.find(remove1, 0);
    BOOST_CHECK_EQUAL(find1.blockHeight(), 0); // we just removed it
    UnspentOutput find2 = db.find(remove1, 1);
    BOOST_CHECK_EQUAL(find2.blockHeight(), 120); // this should not be removed

    UnspentOutput find3 = db.find(remove2, 0);
    BOOST_CHECK_EQUAL(find3.blockHeight(), 189);
    UnspentOutput find4 = db.find(remove2, 1);
    BOOST_CHECK_EQUAL(find4.blockHeight(), 189); // its here now

    success = db.remove(remove2, 1);
    BOOST_CHECK(success);

    UnspentOutput find5 = db.find(remove2, 0);
    BOOST_CHECK_EQUAL(find5.blockHeight(), 189);
    UnspentOutput find6 = db.find(remove2, 1);
    BOOST_CHECK_EQUAL(find6.blockHeight(), 0); // poof.

}

// test if we can keep entries between restarts
BOOST_AUTO_TEST_CASE(restart)
{
    WorkerThreads workers;
    { // scope for DB
        UnspentOutputDatabase db(workers.ioService(), m_testPath);
        insertTransactions(db, 50);
    }

    logDebug() << "Step 2";
    { // scope for DB
        UnspentOutputDatabase db(workers.ioService(), m_testPath);

        for (int i = 0; i < 50; ++i) {
            // logDebug() << "select" << i;
            uint256 txid = insertedTxId(i);
            UnspentOutput uo = db.find(txid, 0);
            BOOST_CHECK_EQUAL(uo.blockHeight(), 100 + i);
            BOOST_CHECK_EQUAL(uo.offsetInBlock(), 6000 + i);

            UnspentOutput uo2 = db.find(txid, 1);
            BOOST_CHECK_EQUAL(uo2.blockHeight(), 100 + i);
            BOOST_CHECK_EQUAL(uo2.offsetInBlock(), 6000 + i);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
