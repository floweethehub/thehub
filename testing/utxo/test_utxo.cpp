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
#include "test_utxo.h"
#include <server/chainparams.h>

#include <WorkerThreads.h>
#include <util.h>

#include <utxo/UnspentOutputDatabase_p.h>

void TestUtxo::init()
{
    m_testPath = GetTempPath() / strprintf("test_flowee_%lu", (unsigned long)GetTime());
    boost::filesystem::remove_all(m_testPath);
}

void TestUtxo::cleanup()
{
    boost::filesystem::remove_all(m_testPath);
}

void TestUtxo::insertTransactions(UnspentOutputDatabase &db, int number)
{
    for (int i = 0; i < number; ++i) {
        char buf[67];
        sprintf(buf, templateTxId, i);
        uint256 txid = uint256S(buf);
        db.insert(txid, 0, 100+i, 6000+i);
        db.insert(txid, 1, 100+i, 6000+i);

        UnspentOutput uo = db.find(txid, 0);
        QCOMPARE(uo.offsetInBlock(), 6000 + i);
        QCOMPARE(uo.blockHeight(), 100 + i);
    }
}

uint256 TestUtxo::insertedTxId(int index)
{
    char buf[67];
    sprintf(buf, templateTxId, index);
    return uint256S(buf);
}


void TestUtxo::basic()
{
    boost::asio::io_service ioService;
    UnspentOutputDatabase db(ioService, m_testPath);
    uint256 txid = uint256S("0xb4749f017444b051c44dfd2720e88f314ff94f3dd6d56d40ef65854fcd7fff6b");
    db.insert(txid, 0, 100, 6000);
    UnspentOutput uo = db.find(txid, 0);
    QCOMPARE(uo.offsetInBlock(), 6000);
    QCOMPARE(uo.blockHeight(), 100);
    QVERIFY(((uo.rmHint() >> 32) & 0xFFFFF) > 0);

    SpentOutput rmData = db.remove(txid, 0, uo.rmHint());
    QVERIFY(rmData.isValid());
    QCOMPARE(rmData.blockHeight, 100);
    QCOMPARE(rmData.offsetInBlock, 6000);

    UnspentOutput uo2 = db.find(txid, 0);
    QCOMPARE(uo2.blockHeight(), 0);

    rmData = db.remove(txid, 0);
    QCOMPARE(rmData.isValid(), false);
    QVERIFY(rmData.blockHeight <= 0);
}

// test if we can keep multiple entries separate
void TestUtxo::multiple()
{
    boost::asio::io_service ioService;
    UnspentOutputDatabase db(ioService, m_testPath);

    insertTransactions(db, 100);
    const uint256 remove1 = insertedTxId(20);
    const uint256 remove2 = insertedTxId(89);
    SpentOutput rmData = db.remove(remove1, 0);
    QVERIFY(rmData.isValid());
    QCOMPARE(rmData.blockHeight, 120);
    QCOMPARE(rmData.offsetInBlock, 6020);

    UnspentOutput find1 = db.find(remove1, 0);
    QCOMPARE(find1.blockHeight(), 0); // we just removed it
    UnspentOutput find2 = db.find(remove1, 1);
    QCOMPARE(find2.blockHeight(), 120); // this should not be removed

    UnspentOutput find3 = db.find(remove2, 0);
    QCOMPARE(find3.blockHeight(), 189);
    UnspentOutput find4 = db.find(remove2, 1);
    QCOMPARE(find4.blockHeight(), 189); // its here now

    rmData = db.remove(remove2, 1);
    QVERIFY(rmData.isValid());
    QCOMPARE(rmData.blockHeight, 189);
    QCOMPARE(rmData.offsetInBlock, 6089);

    UnspentOutput find5 = db.find(remove2, 0);
    QCOMPARE(find5.blockHeight(), 189);
    UnspentOutput find6 = db.find(remove2, 1);
    QCOMPARE(find6.blockHeight(), 0); // poof.
}

// test if we can keep entries between restarts
void TestUtxo::restart()
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
            QCOMPARE(uo.blockHeight(), 100 + i);
            QCOMPARE(uo.offsetInBlock(), 6000 + i);

            UnspentOutput uo2 = db.find(txid, 1);
            QCOMPARE(uo2.blockHeight(), 100 + i);
            QCOMPARE(uo2.offsetInBlock(), 6000 + i);
        }
    }
}

void TestUtxo::commit()
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
        QVERIFY(rmData.isValid());
    }

    {   // usecase 2
        UnspentOutputDatabase db(ioService, m_testPath);
        // after a restart, the not committed tx is again there.
        SpentOutput rmData = db.remove(txid, 0);
        QVERIFY(rmData.isValid());
    }

    {   // usecase 2 && 2
        UnspentOutputDatabase db(ioService, m_testPath);
        // after a restart, the not committed tx is again there.
        SpentOutput rmData = db.remove(txid, 0);
        QVERIFY(rmData.isValid());

        db.rollback();
        rmData = db.remove(txid, 0); // it reappeared
        QVERIFY(rmData.isValid());

        db.blockFinished(2, uint256()); // commit

        rmData = db.remove(txid, 0);
        QVERIFY(!rmData.isValid());
    }

    {
        UnspentOutputDatabase db(ioService, m_testPath);
        // the commit made the removed tx actually go away.
        SpentOutput rmData = db.remove(txid, 0);
        QVERIFY(!rmData.isValid());
    }

    // because the helper method insertTransactions generates transactions
    // that all land in the same bucket I need to create a new one to test buckets with only one tx.
    const char *txid2 = "0x1a3454117444b051c44dfd2720e88f314ff94f3dd6d56d40ef65854fcd7fff6b";

    {
        UnspentOutputDatabase db(ioService, m_testPath);
        db.insert(uint256S(txid2), 0, 200, 2000);
        SpentOutput rmData = db.remove(uint256S(txid2), 0);
        QVERIFY(rmData.isValid()); // delete should be Ok
    }

    {
        UnspentOutputDatabase db(ioService, m_testPath);
        // test usecase 5
        SpentOutput rmData = db.remove(uint256S(txid2), 0);
        QVERIFY(!rmData.isValid()); // it was never committed

        // test usecase 1
        db.insert(uint256S(txid2), 0, 200, 2000);
        db.blockFinished(3, uint256());
    }
    {
        // continue to test usecase 1
        UnspentOutputDatabase db(ioService, m_testPath);
        SpentOutput rmData = db.remove(uint256S(txid2), 0);
        QVERIFY(rmData.isValid());
    }
    {
        // continue to test usecase 1
        UnspentOutputDatabase db(ioService, m_testPath);
        SpentOutput rmData = db.remove(uint256S(txid2), 0);
        QVERIFY(rmData.isValid()); // it came back!
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
        QVERIFY(!uo.isValid()); // it was never committed
    }

    // test usecase 5
    char buf[67];
    sprintf(buf, templateTxId, 200);
    uint256 txid4 = uint256S(buf);
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        db.insert(txid4, 5, 40, 81);
    }
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        QVERIFY(!db.find(txid4, 5).isValid());
    }
    // now separate the saving of the bucket and the leafs.
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        db.insert(txid4, 6, 40, 81);
        db.blockFinished(4, uint256());
        db.insert(txid4, 7, 40, 81);
    }
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        QVERIFY(!db.find(txid4, 5).isValid());
        QVERIFY(db.find(txid4, 6).isValid());
        QVERIFY(!db.find(txid4, 7).isValid());
    }

    // new usecase; deleting from an in-memory bucket.
    // A bucket was saved to disk, retrieved and stored in memory because I inserted
    // a new item and then I remove an old item.
    // We need to make sure that the on-disk bucket is the one we get after rollback()
    sprintf(buf, templateTxId, 127);
    uint256 txid5 = uint256S(buf);
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        db.insert(txid5, 10, 40, 90);
        db.insert(txid5, 11, 40, 90);
        db.insert(txid5, 13, 40, 90);
        db.blockFinished(5, uint256());
    }
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        db.insert(txid5, 20, 40, 81); // loads from disk, adds item
        // rollback now should revert to the on-disk version.
    }
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        QVERIFY(db.find(txid4, 6).isValid());
        QVERIFY(!db.find(txid5, 20).isValid());
        QVERIFY(db.find(txid5, 10).isValid());
        QVERIFY(db.find(txid5, 11).isValid());
        QVERIFY(db.find(txid5, 13).isValid());
    }
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        db.insert(txid5, 20, 40, 81); // loads from disk, adds item
        SpentOutput rmData = db.remove(txid5, 11); // removes from mem-bucket
        QVERIFY(rmData.isValid());
        // rollback now should revert to the on-disk version.
    }
    {
        UnspentOutputDatabase db(ioService, m_testPath);
        QVERIFY(db.find(txid4, 6).isValid());
        QVERIFY(!db.find(txid5, 20).isValid());
        QVERIFY(db.find(txid5, 10).isValid());
        QVERIFY(db.find(txid5, 11).isValid());
        QVERIFY(db.find(txid5, 13).isValid());
    }
}

void TestUtxo::cowList()
{
    DataFileList list;
    auto x = DataFile::createDatafile((m_testPath / "testdb").string(), 1, uint256());
    list.append(x);
    QVERIFY(x == list[0]);
    QVERIFY(x == list.at(0));

    auto copy(list);
    QVERIFY(x == copy[0]);
    QVERIFY(x == copy.at(0));

    copy[0] = nullptr;
    QVERIFY(x == list[0]);
    QVERIFY(x == list.at(0));
    QVERIFY(nullptr == copy[0]);
    QVERIFY(nullptr == copy.at(0));

    delete x;
}

QTEST_MAIN(TestUtxo)
