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

#include "TestLive.h"

#include <Message.h>

#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

void TestApiLive::testBasic()
{
    startHubs();
    QCOMPARE((int) con.size(), 1);

    con[0].send(Message(Api::APIService, Api::Meta::Version));
    Message m = waitForMessage(0, Api::APIService, Api::Meta::VersionReply, Api::Meta::Version);
    QCOMPARE((int) m.serviceId(), (int) Api::APIService);
    QCOMPARE((int) m.messageId(), (int) Api::Meta::VersionReply);
    Streaming::MessageParser parser(m.body());
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::GenericByteData) {
            QVERIFY(parser.isString());
            QVERIFY(QString::fromStdString(parser.stringData()).startsWith("Flowee:"));
            return;
        }
    }
    QVERIFY(false); // version not included in reply
}

void TestApiLive::testSendTx()
{
    startHubs();
    con[0].send(Message(Api::UtilService, Api::Util::CreateAddress));
    Message m = waitForMessage(0, Api::UtilService, Api::Util::CreateAddressReply, Api::Util::CreateAddress);
    Streaming::MessageParser parser(m.body());
    Streaming::ConstBuffer address;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::Util::BitcoinAddress) {
            address = parser.bytesDataBuffer();
            break;
        }
    }

    Streaming::MessageBuilder builder(Streaming::NoHeader, 100000);
    builder.add(Api::RegTest::BitcoinAddress, address);
    builder.add(Api::RegTest::Amount, 101);
    con[0].send(builder.message(Api::RegTestService, Api::RegTest::GenerateBlock));
    m = waitForMessage(0, Api::RegTestService, Api::RegTest::GenerateBlockReply, Api::RegTest::GenerateBlock);
    QCOMPARE(m.serviceId(), (int) Api::RegTestService);

    builder.add(Api::BlockChain::BlockHeight, 2);
    con[0].send(builder.message(Api::BlockChainService, Api::BlockChain::GetBlock));
    m = waitForMessage(0, Api::BlockChainService, Api::BlockChain::GetBlockReply, Api::BlockChain::GetBlock);
    QCOMPARE(m.serviceId(), (int) Api::BlockChainService);
    QCOMPARE(m.messageId(), (int) Api::BlockChain::GetBlockReply);
    Streaming::ConstBuffer coinbase;
    parser = Streaming::MessageParser(m.body());
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::BlockChain::GenericByteData) {
            coinbase = parser.bytesDataBuffer();
            break;
        }
    }
    QVERIFY(coinbase.size() > 0);
    m_hubs[0].messages.clear();
    for (int i = 0; i < 100; ++i) {
        builder.add(Api::LiveTransactions::Transaction, coinbase);
        con[0].send(builder.message(Api::LiveTransactionService, Api::LiveTransactions::SendTransaction));
    }
    con[0].send(Message(Api::APIService, Api::Meta::Version));
    waitForMessage(0, Api::APIService, Api::Meta::VersionReply, Api::Meta::Version);
    auto messages = m_hubs[0].messages;
    QCOMPARE((int) messages.size(), 101);
    for (int i = 0; i < 100; ++i) {
        // Streaming::MessageParser::debugMessage(messages[i]);
        QCOMPARE(messages[i].messageId(), (int) Api::Meta::CommandFailed);
        QCOMPARE(messages[i].serviceId(), (int) Api::APIService);
    }
}
