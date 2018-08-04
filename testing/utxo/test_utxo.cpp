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
    BasicFixture() { setup(); }
    ~BasicFixture() { teardown(); }
    void setup() {
        // in 1.61 this only gets called from constructor, in 1.67 this gets called twice.
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
            db.insert(txid, 0, 100+i, 6000+i);
            db.insert(txid, 1, 100+i, 6000+i);

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
    db.insert(txid, 0, 100, 6000);
    UnspentOutput uo = db.find(txid, 0);
    BOOST_CHECK_EQUAL(uo.offsetInBlock(), 6000);
    BOOST_CHECK_EQUAL(uo.blockHeight(), 100);

    SpentOutput rmData = db.remove(txid, 0);
    BOOST_CHECK(rmData.isValid());
    BOOST_CHECK_EQUAL(rmData.blockHeight, 100);
    BOOST_CHECK_EQUAL(rmData.offsetInBlock, 6000);

    UnspentOutput uo2 = db.find(txid, 0);
    BOOST_CHECK_EQUAL(uo2.blockHeight(), 0);

    rmData = db.remove(txid, 0);
    BOOST_CHECK_EQUAL(rmData.isValid(), false);
    BOOST_CHECK(rmData.blockHeight <= 0);
}

// test if we can keep multiple entries separate
BOOST_AUTO_TEST_CASE(multiple)
{
    boost::asio::io_service ioService;
    UnspentOutputDatabase db(ioService, m_testPath);

    insertTransactions(db, 100);
    const uint256 remove1 = insertedTxId(20);
    const uint256 remove2 = insertedTxId(89);
    SpentOutput rmData = db.remove(remove1, 0);
    BOOST_CHECK(rmData.isValid());
    BOOST_CHECK_EQUAL(rmData.blockHeight, 120);
    BOOST_CHECK_EQUAL(rmData.offsetInBlock, 6020);

    UnspentOutput find1 = db.find(remove1, 0);
    BOOST_CHECK_EQUAL(find1.blockHeight(), 0); // we just removed it
    UnspentOutput find2 = db.find(remove1, 1);
    BOOST_CHECK_EQUAL(find2.blockHeight(), 120); // this should not be removed

    UnspentOutput find3 = db.find(remove2, 0);
    BOOST_CHECK_EQUAL(find3.blockHeight(), 189);
    UnspentOutput find4 = db.find(remove2, 1);
    BOOST_CHECK_EQUAL(find4.blockHeight(), 189); // its here now

    rmData = db.remove(remove2, 1);
    BOOST_CHECK(rmData.isValid());
    BOOST_CHECK_EQUAL(rmData.blockHeight, 189);
    BOOST_CHECK_EQUAL(rmData.offsetInBlock, 6089);

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
        db.blockFinished(1, uint256()); // commmit
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

BOOST_AUTO_TEST_CASE(commit)
{
    /*
     * delete is by far the most complex usecase.
     * I should test;
     *   1) delete a leaf from an on-disk bucket that only contains the one item
     *   2) delete a leaf from an on-disk bucket where there are more leafs
     *   3) delete a leaf from an in-memory bucket where there are more leafs
     *   4) delete a leaf from an in-memory bucket where its the last leaf
     *
     * Also I should create a new leaf
     *   5) in an existing bucket
     *   6) in a new bucket
     */
    boost::asio::io_service ioService;
    uint256 txid;
    {   // usecase 3
        UnspentOutputDatabase db(ioService, m_testPath);
        insertTransactions(db, 100);
        db.blockFinished(1, uint256()); // this is a 'commit'

        txid = insertedTxId(99);
        SpentOutput rmData = db.remove(txid, 0);
        BOOST_CHECK(rmData.isValid());
    }

    {   // usecase 2
        UnspentOutputDatabase db(ioService, m_testPath);
        // after a restart, the not committed tx is again there.
        SpentOutput rmData = db.remove(txid, 0);
        BOOST_CHECK(rmData.isValid());
    }

    {   // usecase 2 && 2
        UnspentOutputDatabase db(ioService, m_testPath);
        // after a restart, the not committed tx is again there.
        SpentOutput rmData = db.remove(txid, 0);
        BOOST_CHECK(rmData.isValid());

        db.rollback();
        rmData = db.remove(txid, 0); // it reappeared
        BOOST_CHECK(rmData.isValid());

        db.blockFinished(2, uint256()); // commit

        rmData = db.remove(txid, 0);
        BOOST_CHECK(!rmData.isValid());
    }

    {
        UnspentOutputDatabase db(ioService, m_testPath);
        // the commit made the removed tx actually go away.
        SpentOutput rmData = db.remove(txid, 0);
        BOOST_CHECK(!rmData.isValid());
    }

    // because the helper method insertTransactions generates transactions
    // that all land in the same bucket I need to create a new one to test buckets with only one tx.
    const char *txid2 = "0x1a3454117444b051c44dfd2720e88f314ff94f3dd6d56d40ef65854fcd7fff6b";

    {
        UnspentOutputDatabase db(ioService, m_testPath);
        db.insert(uint256S(txid2), 0, 200, 2000);
        SpentOutput rmData = db.remove(uint256S(txid2), 0);
        BOOST_CHECK(rmData.isValid()); // delete should be Ok
    }

    {
        UnspentOutputDatabase db(ioService, m_testPath);
        // test usecase 5
        SpentOutput rmData = db.remove(uint256S(txid2), 0);
        BOOST_CHECK(!rmData.isValid()); // it was never committed

        // test usecase 1
        db.insert(uint256S(txid2), 0, 200, 2000);
        db.blockFinished(3, uint256());
    }
    {
        // continue to test usecase 1
        UnspentOutputDatabase db(ioService, m_testPath);
        SpentOutput rmData = db.remove(uint256S(txid2), 0);
        BOOST_CHECK(rmData.isValid());
    }
    {
        // continue to test usecase 1
        UnspentOutputDatabase db(ioService, m_testPath);
        SpentOutput rmData = db.remove(uint256S(txid2), 0);
        BOOST_CHECK(rmData.isValid()); // it came back!
    }

    const char *txid3 = "0x4a3454117444b051c44dfd2720e88f314ff94f3dd6d56d40ef65854fcd7fff6b";
    // usecase 6
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        db.insert(txid3, 2, 300, 1000);
    }
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        UnspentOutput uo = db.find(uint256S(txid3), 2);
        BOOST_CHECK(!uo.isValid()); // it was never committed
    }

    // test usecase 5
    char buf[67];
    sprintf(buf, templateTxId, 200);
    uint256 txid4 = uint256S(buf);
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        db.insert(txid4, 5, 40, 40);
    }
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        BOOST_CHECK(!db.find(txid4, 5).isValid());
    }
    // now separate the saving of the bucket and the leafs.
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        db.insert(txid4, 6, 40, 40);
        db.blockFinished(4, uint256());
        db.insert(txid4, 7, 40, 40);
    }
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        BOOST_CHECK(!db.find(txid4, 5).isValid());
        BOOST_CHECK(db.find(txid4, 6).isValid());
        BOOST_CHECK(!db.find(txid4, 7).isValid());
    }
}

BOOST_AUTO_TEST_SUITE_END()
