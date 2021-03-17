/*
 * This file is part of the Flowee project
 * Copyright (C) 2019-2020 Tom Zander <tomz@freedommail.ch>
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
#include "TestTxIdMonitor.h"
#include "TestData.h"

#include <uint256.h>
#include <streaming/BufferPool.h>
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>
#include <primitives/FastTransaction.h>

void TestTxIdMonitor::testBasic()
{
    startHubs();

    Streaming::BufferPool pool;
    pool.reserve(50);
    Streaming::MessageBuilder builder(pool);

    // (coinbase @ 5)
    builder.add(Api::TransactionMonitor::TxId, uint256S("29bae599098880a9e609418ea5dee1da154b214c3cad6f99d96596badfadefc1"));
    // (last tx in block 111)
    builder.add(Api::TransactionMonitor::TxId, uint256S("a3132383a40424c1ac9644be637c3bb471dffdfa4bf05139b9a0ce3b8d45db88"));
    auto m = waitForReply(0, builder.message(Api::TransactionMonitorService,
                                          Api::TransactionMonitor::Subscribe), Api::TransactionMonitor::SubscribeReply);
    QVERIFY(m.messageId() == Api::TransactionMonitor::SubscribeReply);
    feedDefaultBlocksToHub(0);

    int total = 0;
    for (auto message : m_hubs[0].messages) {
        if (message.serviceId() == Api::TransactionMonitorService) {
            QVERIFY(message.messageId() == Api::TransactionMonitor::SubscribeReply
                    || message.messageId() == Api::TransactionMonitor::TransactionFound);
            if (message.messageId() != Api::TransactionMonitor::TransactionFound)
                continue;
            ++total;
            bool seenBlockHeight = false, seenOffsetInBlock = false;
            int seenTransactions = 0;
            Streaming::MessageParser p(message.body());
            QCOMPARE(p.next(), Streaming::FoundTag);
            while (true) {
                if (p.tag() == Api::TransactionMonitor::TxId) {
                    seenTransactions++;
                    QCOMPARE(p.isByteArray(), true);
                    QCOMPARE(p.dataLength(), 32);
                }
                else if (p.tag() == Api::TransactionMonitor::OffsetInBlock) {
                    seenOffsetInBlock = true;
                    QCOMPARE(p.isInt(), true);
                    QVERIFY(p.intData() > 80);
                }
                else if (p.tag() == Api::TransactionMonitor::BlockHeight) {
                    seenBlockHeight = true;
                    QCOMPARE(p.isInt(), true);
                    QVERIFY(p.intData() > 0);
                }
                auto type = p.next();
                QVERIFY(type != Streaming::Error);
                if (type != Streaming::FoundTag)
                    break;
            }

            QVERIFY(seenTransactions > 0);
            QVERIFY(seenTransactions <= 2);
            QVERIFY(seenOffsetInBlock);
            QVERIFY(seenBlockHeight);
        }
    }
    QCOMPARE(total, 2);
}

void TestTxIdMonitor::testMempool()
{
    startHubs(2);
    feedDefaultBlocksToHub(0);
    QVERIFY(waitForHeight(115)); // make sure all nodes are at the same tip.

    Streaming::BufferPool pool;
    auto txs = TestData::createDoubleSpend(&pool);
    Tx tx1 = txs.first;
    pool.reserve(50);
    Streaming::MessageBuilder builder(pool);
    builder.add(Api::TransactionMonitor::TxId, tx1.createHash());
    auto m = waitForReply(0, builder.message(Api::TransactionMonitorService,
                                          Api::TransactionMonitor::Subscribe), Api::TransactionMonitor::SubscribeReply);
    QVERIFY(m.messageId() == Api::TransactionMonitor::SubscribeReply);

    logDebug() << "Sending tx1 to hub0" << tx1.createHash();
    Streaming::MessageBuilder builder2(pool);
    builder2.add(Api::LiveTransactions::GenericByteData, tx1.data());

    m = waitForReply(0, builder.message(Api::LiveTransactionService, Api::LiveTransactions::SendTransaction),
                     Api::TransactionMonitorService, Api::TransactionMonitor::TransactionFound);
    QVERIFY(m.messageId() == Api::TransactionMonitor::TransactionFound);
    bool seenBlockHeight = false, seenOffsetInBlock = false, seenTxId = false;
    Streaming::MessageParser p(m.body());
    while (p.next() == Streaming::FoundTag) {
        if (p.tag() == Api::TransactionMonitor::TxId) {
            QVERIFY(p.uint256Data() == tx1.createHash());
            seenTxId = true;
        }
        else if (p.tag() == Api::TransactionMonitor::BlockHeight)
            seenBlockHeight = true;
        else if (p.tag() == Api::TransactionMonitor::OffsetInBlock)
            seenOffsetInBlock = true;
        else
            QVERIFY(false);
    }

    QCOMPARE(seenTxId, true);
    QCOMPARE(seenBlockHeight, false);
    QCOMPARE(seenOffsetInBlock, false);

    // second part; to connect to a peer that already has it in the mempool.
    builder2.add(Api::TransactionMonitor::TxId, tx1.createHash());
    m = waitForReply(1, builder2.message(Api::TransactionMonitorService, Api::TransactionMonitor::Subscribe),
                     Api::TransactionMonitorService, Api::TransactionMonitor::TransactionFound);
    QVERIFY(m.messageId() == Api::TransactionMonitor::TransactionFound);

    seenTxId = false;
    p = Streaming::MessageParser(m.body());
    while (p.next() == Streaming::FoundTag) {
        if (p.tag() == Api::TransactionMonitor::TxId) {
            QVERIFY(p.uint256Data() == tx1.createHash());
            seenTxId = true;
        }
        else if (p.tag() == Api::TransactionMonitor::BlockHeight)
            seenBlockHeight = true;
        else if (p.tag() == Api::TransactionMonitor::OffsetInBlock)
            seenOffsetInBlock = true;
        else
            QVERIFY(false);
    }

    QCOMPARE(seenTxId, true);
    QCOMPARE(seenBlockHeight, false);
    QCOMPARE(seenOffsetInBlock, false);
}

void TestTxIdMonitor::testDoubleSpend()
{
    startHubs(2);
    feedDefaultBlocksToHub(0);
    QVERIFY(waitForHeight(115)); // make sure all nodes are at the same tip.

    Streaming::BufferPool pool;
    auto txs = TestData::createDoubleSpend(&pool);
    Tx tx1 = txs.first;
    Tx tx2 = txs.second;

    // subscribing
    Streaming::MessageBuilder builder(pool);
    builder.add(Api::TransactionMonitor::TxId, tx1.createHash());
    auto subScribeMessage = builder.message(Api::TransactionMonitorService, Api::TransactionMonitor::Subscribe);
    auto m = waitForReply(0, subScribeMessage, Api::TransactionMonitor::SubscribeReply);
    QCOMPARE(m.messageId(), (int) Api::TransactionMonitor::SubscribeReply);
    m = waitForReply(1, subScribeMessage, Api::TransactionMonitor::SubscribeReply);
    QCOMPARE(m.messageId(), (int) Api::TransactionMonitor::SubscribeReply);

    // I wills end the txs to peer zero, lets catch the double spend proof on peer 1
    m_hubs[1].m_waitForMessageId = Api::TransactionMonitor::DoubleSpendFound;
    m_hubs[1].m_waitForServiceId = Api::TransactionMonitorService;
    m_hubs[1].m_waitForMessageId2 = -1;
    m_hubs[1].m_foundMessage.store(nullptr);

    logDebug() << "Sending tx1 to hub0" << tx1.createHash();
    Streaming::MessageBuilder builder2(pool);
    builder2.add(Api::LiveTransactions::GenericByteData, tx1.data());
    m = waitForReply(0, builder2.message(Api::LiveTransactionService, Api::LiveTransactions::SendTransaction),
                          Api::TransactionMonitorService, Api::TransactionMonitor::TransactionFound);
    QCOMPARE(m.messageId(), (int) Api::TransactionMonitor::TransactionFound);
    logDebug() << "Sending tx2 to hub0" << tx2.createHash();
    Streaming::MessageBuilder builder3(pool);
    builder3.add(Api::LiveTransactions::GenericByteData, tx2.data());
    m = waitForReply(0, builder3.message(Api::LiveTransactionService, Api::LiveTransactions::SendTransaction),
                          Api::TransactionMonitorService, Api::TransactionMonitor::DoubleSpendFound);
    QCOMPARE(m.messageId(), (int) Api::TransactionMonitor::DoubleSpendFound);

    // we should get a notification about the double spend attempt of the transaction I am interested in (tx1).
    // Lets take a look at the message.

    Streaming::MessageParser p(m);
    p.next();
    QCOMPARE(p.tag(), (uint32_t) Api::TransactionMonitor::TxId);
    QCOMPARE(p.dataLength(), 32);
    QVERIFY((p.uint256Data() == tx1.createHash()));
    p.next();
    QCOMPARE(p.tag(), (uint32_t) Api::TransactionMonitor::TransactionData); // the transaction.
    QCOMPARE(p.dataLength(), tx2.size());

    QTRY_VERIFY_WITH_TIMEOUT(m_hubs[1].m_foundMessage != nullptr, 30000); // this waits until its arrived.
    Message *m2 = m_hubs[1].m_foundMessage.load();
    assert(m2); // the qtry should have failed.

    p = Streaming::MessageParser(*m2);
    p.next();
    QCOMPARE(p.tag(), (uint32_t) Api::TransactionMonitor::TxId);
    QCOMPARE(p.dataLength(), 32);
    QVERIFY((p.uint256Data() == tx1.createHash()));
    p.next();
    QVERIFY(p.tag() == Api::TransactionMonitor::DoubleSpendProofData
            || p.tag() == Api::TransactionMonitor::TransactionData);
}
