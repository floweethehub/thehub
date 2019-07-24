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
#include "TestAddressMonitor.h"

#include <streaming/BufferPool.h>
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

#include <uint256.h>

void TestAddressMonitor::testBasic()
{
    startHubs(1);

    Streaming::BufferPool pool;
    pool.reserve(50);
    Streaming::MessageBuilder builder(pool);
    uint160 address;
    address.SetHex("0x12354c465d473347aa8af8201327eff524e15db4");
    builder.add(Api::AddressMonitor::BitcoinAddress, address);
    address.SetHex("0x0fa606b0f3a7afbc21da18897de305dd2144b1da");
    builder.add(Api::AddressMonitor::BitcoinAddress, address);
    auto m = waitForReply(0, builder.message(Api::AddressMonitorService,
                                          Api::AddressMonitor::Subscribe), Api::AddressMonitor::SubscribeReply);

    QVERIFY(m.messageId() == Api::AddressMonitor::SubscribeReply);

    Streaming::MessageParser::debugMessage(m);

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
                if (p.tag() == Api::AddressMonitor::BitcoinAddress) {
                    seenAddress++;
                    QCOMPARE(p.isByteArray(), true);
                    QCOMPARE(p.dataLength(), 20);
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
    QCOMPARE(total, 19);
}
