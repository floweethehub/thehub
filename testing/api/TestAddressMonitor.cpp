/*
 * This file is part of the Flowee project
 * Copyright (C) 2019-2021 Tom Zander <tom@flowee.org>
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
#include "TestAddressMonitor.h"
#include "TestData.h"

#include <streaming/BufferPool.h>
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

#include <DoubleSpendProof.h>
#include <uint256.h>
#include <cashaddr.h>

#include <primitives/FastTransaction.h>

void TestAddressMonitor::testBasic()
{
    startHubs(1);

    Streaming::BufferPool pool;
    pool.reserve(50);
    Streaming::MessageBuilder builder(pool);

    builder.add(Api::AddressMonitor::BitcoinScriptHashed, uint256S("7cbd398b58e489e13100f2f7b0d56f5abc83a2381f9a841434a12447cc7a3b14"));
    builder.add(Api::AddressMonitor::BitcoinScriptHashed, uint256S("00a7a0e144e7050ef5622b098faf19026631401fa46e68a93fe5e5630b94dcea"));
    auto m = waitForReply(0, builder.message(Api::AddressMonitorService,
                                          Api::AddressMonitor::Subscribe), Api::AddressMonitor::SubscribeReply);
    QVERIFY(m.messageId() == Api::AddressMonitor::SubscribeReply);

    feedDefaultBlocksToHub(0);

    int total = 0;
    for (auto message : m_hubs[0].messages) {
        if (message.serviceId() == Api::AddressMonitorService) {
            QVERIFY(message.messageId() == Api::AddressMonitor::SubscribeReply
                    || message.messageId() == Api::AddressMonitor::TransactionFound);
            if (message.messageId() != Api::AddressMonitor::TransactionFound)
                continue;
            ++total;
            bool seenAmount = false, seenOffsetInBlock = false, seenBlockHeight = false;
            int seenAddress = 0;
            Streaming::MessageParser p(message.body());
            QCOMPARE(p.next(), Streaming::FoundTag);
            while (true) {
                if (p.tag() == Api::AddressMonitor::BitcoinScriptHashed) {
                    seenAddress++;
                    QCOMPARE(p.isByteArray(), true);
                    QCOMPARE(p.dataLength(), 32);
                }
                else if (p.tag() == Api::AddressMonitor::Amount) {
                    seenAmount = true;
                    QCOMPARE(p.isLong(), true);
                    QVERIFY(p.longData() > 0);
                }
                else if (p.tag() == Api::AddressMonitor::OffsetInBlock) {
                    seenOffsetInBlock = true;
                    QCOMPARE(p.isInt(), true);
                    QVERIFY(p.intData() > 80);
                }
                else if (p.tag() == Api::AddressMonitor::BlockHeight) {
                    seenBlockHeight = true;
                    QCOMPARE(p.isInt(), true);
                    QVERIFY(p.intData() > 0);
                }
                auto type = p.next();
                QVERIFY(type != Streaming::Error);
                if (type != Streaming::FoundTag)
                    break;
            }

            QVERIFY(seenAddress > 0);
            QVERIFY(seenAddress <= 2);
            QVERIFY(seenAmount);
            QVERIFY(seenOffsetInBlock);
            QVERIFY(seenBlockHeight);
        }
    }
    QCOMPARE(total, 196);
}

void TestAddressMonitor::testDoubleSpendProof()
{
    /*
     * In this test we use the existing testing chain and spend one of the outputs twice.
     * Both transactions have 1 in, 1 out.
     * The outputs are different and the addresses are the ones hardcoded in the onConnected()
     * method so the monitor should always trigger on our sending those tx's to the nodes.
     *
     * We first send one tx to one node, then (after waiting for propagation) the other to
     * the second node.
     * Then we wait for the double spend proofs to be send to us from both nodes.
     * The last node that actually saw the double spend just parrots it to us, with a transaction
     * and not a proof.
     * Then the other node (the first) should get a double spend as proof over p2p which we should
     * get from the monitor.
     */

    // on connect, always subscribe to the two outputs that
    struct MonitorAddressesInit {
        explicit MonitorAddressesInit(NetworkManager *nm) : network(nm) { Q_ASSERT(nm); }
        void onConnected(const EndPoint &ep) {
            Streaming::BufferPool pool;
            pool.reserve(50);
            Streaming::MessageBuilder builder(pool);
            // this is a hash of the full script of a p2pkh output script (see CashAddress::createHashedOutputScript())
            builder.add(Api::AddressMonitor::BitcoinScriptHashed, uint256S("7c3cb6eb855660b775bbe66e1c245beb405000cc1c5374771a474051685b6e33"));
            builder.add(Api::AddressMonitor::BitcoinScriptHashed, uint256S("f324a872150702b3ba647c5fc39a5c8d36519b2d1430109321a89112102f3ec8"));
            auto subscribeMessage = builder.message(Api::AddressMonitorService, Api::AddressMonitor::Subscribe);
            network->connection(ep).send(subscribeMessage);
        }
    private:
        NetworkManager *network;
    };

    MonitorAddressesInit subscriber(&m_network);
    m_onConnectCallbacks.push_back(std::bind(&MonitorAddressesInit::onConnected, &subscriber, std::placeholders::_1));
    m_onConnectCallbacks.push_back(std::bind(&MonitorAddressesInit::onConnected, &subscriber, std::placeholders::_1));

    startHubs(2);
    feedDefaultBlocksToHub(0);
    QVERIFY(waitForHeight(115)); // make sure all nodes are at the same tip.

    Streaming::BufferPool pool;
    auto txs = TestData::createDoubleSpend(&pool);
    Tx tx1 = txs.first;
    Tx tx2 = txs.second;

    logDebug() << "Sending tx1 to hub0" << tx1.createHash();

    // I sent one tx to peer zero, and wait for it to be synchronized on peer 1 as well.
    m_hubs[1].m_waitForMessageId = Api::AddressMonitor::TransactionFound;
    m_hubs[1].m_waitForServiceId = Api::AddressMonitorService;
    m_hubs[1].m_waitForMessageId2 = -1;
    m_hubs[1].m_foundMessage.store(nullptr);

    Streaming::MessageBuilder builder(pool);
    builder.add(Api::LiveTransactions::GenericByteData, tx1.data());
    con[0].send(builder.message(Api::LiveTransactionService, Api::LiveTransactions::SendTransaction));

    QTRY_VERIFY_WITH_TIMEOUT(m_hubs[1].m_foundMessage.load() != nullptr, 50000);
    auto m = *m_hubs[1].m_foundMessage.load();
    QCOMPARE(m.serviceId(), (int) Api::AddressMonitorService);
    QCOMPARE(m.messageId(), (int) Api::AddressMonitor::TransactionFound);


    // now we send the second tx and expect a double-spend-notification from both peers.
    // This will be propagated by proof between them.

    m_hubs[0].m_waitForMessageId = m_hubs[1].m_waitForMessageId = Api::AddressMonitor::DoubleSpendFound;
    m_hubs[0].m_waitForServiceId = Api::AddressMonitorService;
    m_hubs[0].m_waitForMessageId2 = -1;
    m_hubs[0].m_foundMessage.store(nullptr);
    m_hubs[1].m_foundMessage.store(nullptr);

    logDebug() << "Sending tx2 to hub1" << tx2.createHash();
    // double-spend-tx (same input, other output and amount)
    Streaming::MessageBuilder builder2(pool);
    builder2.add(Api::LiveTransactions::GenericByteData, tx2.data());
    // Sent this to HUB 1
    con[1].send(builder2.message(Api::LiveTransactionService, Api::LiveTransactions::SendTransaction));


    // from hub 1 I should have received an old fashioned double spend. The one with TX.
    QTRY_VERIFY(m_hubs[1].m_foundMessage.load() != nullptr);
    m = *m_hubs[1].m_foundMessage.load();
    QCOMPARE(m.serviceId(), (int) Api::AddressMonitorService);
    QCOMPARE(m.messageId(), (int) Api::AddressMonitor::DoubleSpendFound);

    Streaming::MessageParser p(m);
    p.next();
    QCOMPARE(p.tag(), (uint32_t) Api::AddressMonitor::BitcoinScriptHashed);
    QCOMPARE(p.dataLength(), 32);
    p.next();
    QCOMPARE(p.tag(), (uint32_t) Api::AddressMonitor::BitcoinScriptHashed);
    QCOMPARE(p.dataLength(), 32);
    p.next();
    QCOMPARE(p.tag(), (uint32_t) Api::AddressMonitor::Amount);

    // the amounts order isn't known, just make sure each appears once.
    QSet<uint64_t> amounts = {12494842l ,12484842l };
    auto f = amounts.find(p.longData());
    QVERIFY(f != amounts.end());
    amounts.erase(f);
    p.next();
    QCOMPARE(p.tag(), (uint32_t) Api::AddressMonitor::Amount);
    f = amounts.find(p.longData());
    QVERIFY(f != amounts.end());
    p.next();
    QCOMPARE(p.tag(), (uint32_t) Api::AddressMonitor::TxId);
    QCOMPARE(p.dataLength(), 32);
    p.next();
    QCOMPARE(p.tag(), (uint32_t) Api::AddressMonitor::TransactionData); // the transaction.
    QCOMPARE(p.dataLength(), 192);


    // From peer 0 I get a double spend notifaction as well, but one based on the proof.
    QTRY_VERIFY(m_hubs[0].m_foundMessage.load() != nullptr);
    m = *m_hubs[0].m_foundMessage.load();

    p = Streaming::MessageParser(m);
    p.next();
    QCOMPARE(p.tag(), (uint32_t) Api::AddressMonitor::BitcoinScriptHashed);
    QCOMPARE(p.dataLength(), 32);
    p.next();
    QCOMPARE(p.tag(), (uint32_t) Api::AddressMonitor::Amount);
    QCOMPARE(p.longData(), (uint64_t) 12494842);
    p.next();
    QCOMPARE(p.tag(), (uint32_t) Api::AddressMonitor::TxId);
    QCOMPARE(p.dataLength(), 32);
    p.next();
    QCOMPARE(p.tag(), (uint32_t) Api::AddressMonitor::DoubleSpendProofData); // the proof
    QCOMPARE(p.dataLength(), 400);

    QFile dsp("/home/zander/dsproof");
    dsp.open(QIODevice::WriteOnly);
    auto data = p.bytesDataBuffer();
    dsp.write(data.begin(), data.size());
}
