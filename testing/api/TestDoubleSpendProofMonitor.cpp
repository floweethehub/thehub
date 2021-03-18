/*
 * This file is part of the Flowee project
 * Copyright (C) 2021 Tom Zander <tom@flowee.org>
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
#include "TestDoubleSpendProofMonitor.h"
#include "TestData.h"

#include <streaming/BufferPool.h>
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>
#include <primitives/FastTransaction.h>

void TestDoubleSpendMonitor::testBasic()
{
    startHubs(2);
    feedDefaultBlocksToHub(0);
    QVERIFY(waitForHeight(115)); // make sure all nodes are at the same tip.

    // subscribe to the dsproof monitor on both nodes.
    Message subscribe(Api::DoubleSpendNotificationService, Api::DSP::Subscribe);
    con[0].send(subscribe);
    con[1].send(subscribe);

    // Prepare both to detect the double spend message
    m_hubs[0].m_waitForMessageId = Api::DSP::NewDoubleSpend;
    m_hubs[0].m_waitForServiceId = Api::DoubleSpendNotificationService;
    m_hubs[0].m_waitForMessageId2 = -1;
    m_hubs[0].m_foundMessage.store(nullptr);
    m_hubs[1].m_waitForMessageId = Api::DSP::NewDoubleSpend;
    m_hubs[1].m_waitForServiceId = Api::DoubleSpendNotificationService;
    m_hubs[1].m_waitForMessageId2 = -1;
    m_hubs[1].m_foundMessage.store(nullptr);

    Streaming::BufferPool pool;
    auto txs = TestData::createDoubleSpend(&pool);
    Streaming::MessageBuilder builder(pool);
    builder.add(Api::LiveTransactions::GenericByteData, txs.first.data());
    con[0].send(builder.message(Api::LiveTransactionService, Api::LiveTransactions::SendTransaction));
    builder.add(Api::LiveTransactions::GenericByteData, txs.second.data());
    con[1].send(builder.message(Api::LiveTransactionService, Api::LiveTransactions::SendTransaction));

    QTRY_VERIFY_WITH_TIMEOUT(m_hubs[0].m_foundMessage.load() != nullptr, 50000);
    auto m = *m_hubs[0].m_foundMessage.load();
    Streaming::MessageParser parser(m);
    bool seenTxId = false;
    bool seenDSP = false;
    bool seenTx = false;
    while (parser.next() != Streaming::EndOfDocument) {
        if (parser.tag() == Api::DSP::TxId) {
            QCOMPARE(parser.isByteArray(), true);
            QCOMPARE(parser.dataLength(), 32);
            QVERIFY(!seenTxId);
            seenTxId = true;
        }
        else if (parser.tag() == Api::DSP::DoubleSpendProofData) {
            QCOMPARE(parser.isByteArray(), true);
            QVERIFY(parser.dataLength() > 300);
            QVERIFY(!seenDSP);
            seenDSP = true;
        }
        else if (parser.tag() == Api::DSP::Transaction) {
            QCOMPARE(parser.isByteArray(), true);
            QVERIFY(parser.dataLength() > 150);
            QVERIFY(!seenTx);
            seenTx = true;
        }
        else {
            QVERIFY(false);
        }
    }
    QVERIFY(seenTxId);
    QVERIFY(seenDSP || seenTx);

    QTRY_VERIFY_WITH_TIMEOUT(m_hubs[1].m_foundMessage.load() != nullptr, 50000);
    m = *m_hubs[1].m_foundMessage.load();
    Streaming::MessageParser parser2(m);
    seenTxId = false;
    seenDSP = false;
    seenTx = false;
    while (parser2.next() != Streaming::EndOfDocument) {
        if (parser2.tag() == Api::DSP::TxId) {
            QCOMPARE(parser2.isByteArray(), true);
            QCOMPARE(parser2.dataLength(), 32);
            QVERIFY(!seenTxId);
            seenTxId = true;
        }
        else if (parser2.tag() == Api::DSP::DoubleSpendProofData) {
            QCOMPARE(parser2.isByteArray(), true);
            QVERIFY(parser2.dataLength() > 300);
            QVERIFY(!seenDSP);
            seenDSP = true;
        }
        else if (parser2.tag() == Api::DSP::Transaction) {
            QCOMPARE(parser2.isByteArray(), true);
            QVERIFY(parser2.dataLength() > 150);
            QVERIFY(!seenTx);
            seenTx = true;
        }
        else {
            QVERIFY(false);
        }
    }
    QVERIFY(seenTxId);
    QVERIFY(seenDSP || seenTx);
}
