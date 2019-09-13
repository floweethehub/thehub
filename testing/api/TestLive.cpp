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

    Message m = waitForReply(0, Message(Api::APIService, Api::Meta::Version), Api::Meta::VersionReply);
    QCOMPARE(m.serviceId(), (int) Api::APIService);
    QCOMPARE(m.messageId(), (int) Api::Meta::VersionReply);
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
    generate100();
    Streaming::MessageBuilder builder(Streaming::NoHeader, 100000);
    builder.add(Api::BlockChain::BlockHeight, 2);
    Message m = waitForReply(0, builder.message(Api::BlockChainService, Api::BlockChain::GetBlock), Api::BlockChain::GetBlockReply);
    QCOMPARE(m.serviceId(), (int) Api::BlockChainService);
    QCOMPARE(m.messageId(), (int) Api::BlockChain::GetBlockReply);
    Streaming::ConstBuffer coinbase;
    Streaming::MessageParser parser(m.body());
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
    waitForReply(0, Message(Api::APIService, Api::Meta::Version), Api::Meta::VersionReply);
    auto messages = m_hubs[0].messages;
    QCOMPARE((int) messages.size(), 101);
    for (size_t i = 0; i < 100; ++i) {
        // Streaming::MessageParser::debugMessage(messages[i]);
        QCOMPARE(messages[i].messageId(), (int) Api::Meta::CommandFailed);
        QCOMPARE(messages[i].serviceId(), (int) Api::APIService);
    }
}

void TestApiLive::testUtxo()
{
    startHubs();
    generate100();

    Streaming::MessageBuilder builder(Streaming::NoHeader);
    builder.add(Api::BlockChain::BlockHeight, 2);
    builder.add(Api::BlockChain::Include_TxId, true);
    Message m = waitForReply(0, builder.message(Api::BlockChainService, Api::BlockChain::GetBlock), Api::BlockChain::GetBlockReply);
    uint256 txid;
    Streaming::MessageParser parser(m.body());
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::BlockChain::TxId) {
            QVERIFY(parser.isByteArray());
            QCOMPARE(parser.dataLength(), 32);
            txid = parser.uint256Data();
            break;
        }
    }
    QVERIFY(!txid.IsNull());

    builder.add(Api::LiveTransactions::TxId, uint256S("0x1111111111111111111111111111111111111111111111111111111111111111"));
    builder.add(Api::LiveTransactions::OutIndex, 1);
    builder.add(Api::Separator, true);
    builder.add(Api::LiveTransactions::TxId, txid);
    builder.add(Api::LiveTransactions::OutIndex, 1);
    builder.add(Api::Separator, false); // mix things up a little
    builder.add(Api::LiveTransactions::TxId, txid);
    builder.add(Api::LiveTransactions::OutIndex, 0);
    Message request = builder.message(Api::LiveTransactionService, Api::LiveTransactions::IsUnspent);
    m = waitForReply(0, request, Api::LiveTransactions::IsUnspentReply);
    QCOMPARE(m.serviceId(), (int) Api::LiveTransactionService);
    QCOMPARE(m.messageId(), (int) Api::LiveTransactions::IsUnspentReply);

    parser = Streaming::MessageParser(m.body());
    int index = 0;
    int index2 = 0;
    bool seenBlockHeight = false;
    bool seenOffsetInBlock = false;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::LiveTransactions::UnspentState) {
            switch (index) {
            case 0:
            case 1:
                QCOMPARE(parser.isBool(), true);
                QCOMPARE(parser.boolData(), false);
                break;
            case 2:
                QCOMPARE(parser.isBool(), true);
                QCOMPARE(parser.boolData(), true);
                break;
            }
        } else if (parser.tag() == Api::Separator ) {
            QCOMPARE(seenBlockHeight, index == 2);
            QCOMPARE(seenOffsetInBlock, index == 2);
            QCOMPARE(index++, index2++);
        } else if (parser.tag() == Api::LiveTransactions::BlockHeight) {
            QCOMPARE(index, 2);
            QVERIFY(parser.isInt());
            QCOMPARE(parser.intData(), 2);
            seenBlockHeight = true;
        } else if (parser.tag() == Api::LiveTransactions::OffsetInBlock) {
            QCOMPARE(index, 2);
            QVERIFY(parser.isInt());
            QCOMPARE(parser.intData(), 81);
            seenOffsetInBlock = true;
        } else {
            logFatal() << "tag that doesn't belong:" << parser.tag();
            QVERIFY(false);
        }
    }
    QCOMPARE(index, 2);
    QCOMPARE(seenBlockHeight, true);
    QCOMPARE(seenOffsetInBlock, true);

    request.setMessageId(Api::LiveTransactions::GetUnspentOutput);
    con[0].send(request);
    m = waitForReply(0, request, Api::LiveTransactions::GetUnspentOutputReply);
    index = index2 = 0;
    parser = Streaming::MessageParser(m.body());
    seenBlockHeight = false;
    seenOffsetInBlock = false;
    bool seenAmount = false;
    bool seenOutputScript = false;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::LiveTransactions::UnspentState) {
            switch (index) {
            case 0:
            case 1:
                QCOMPARE(parser.isBool(), true);
                QCOMPARE(parser.boolData(), false);
                break;
            case 2:
                QCOMPARE(parser.isBool(), true);
                QCOMPARE(parser.boolData(), true);
                break;
            }
        } else if (parser.tag() == Api::LiveTransactions::BlockHeight) {
            QCOMPARE(index, 2);
            QVERIFY(parser.isInt());
            QCOMPARE(parser.intData(), 2);
            seenBlockHeight = true;
        } else if (parser.tag() == Api::LiveTransactions::OffsetInBlock) {
            QCOMPARE(index, 2);
            QVERIFY(parser.isInt());
            QCOMPARE(parser.intData(), 81);
            seenOffsetInBlock = true;
        } else if (parser.tag() == Api::LiveTransactions::Amount) {
            QCOMPARE(index, 2);
            QVERIFY(parser.isLong());
            QCOMPARE(parser.longData(), (uint64_t) 5000000000);
            seenAmount = true;
        } else if (parser.tag() == Api::LiveTransactions::OutputScript) {
            QCOMPARE(index, 2);
            QVERIFY(parser.isByteArray());
            seenOutputScript = true;
        } else if (parser.tag() == Api::Separator ) {
            QCOMPARE(seenBlockHeight, index == 2);
            QCOMPARE(seenOffsetInBlock, index == 2);
            QCOMPARE(seenAmount, index == 2);
            QCOMPARE(seenOutputScript, index == 2);
            QCOMPARE(index++, index2++);
        } else {
            logFatal() << "tag that doesn't belong:" << parser.tag();
            QVERIFY(false);
        }
    }
    QCOMPARE(index, 2);
    QCOMPARE(seenBlockHeight, true);
    QCOMPARE(seenOffsetInBlock, true);


    // also check fetch using blockheight / offset instead of txid
    builder.add(Api::LiveTransactions::BlockHeight, 2);
    builder.add(Api::LiveTransactions::OffsetInBlock, 81);
    builder.add(Api::LiveTransactions::OutIndex, 0);
    request = builder.message(Api::LiveTransactionService, Api::LiveTransactions::IsUnspent);
    m = waitForReply(0, request, Api::LiveTransactions::IsUnspentReply);
    Streaming::MessageParser::debugMessage(m);
    QCOMPARE(m.serviceId(), (int) Api::LiveTransactionService);
    QCOMPARE(m.messageId(), (int) Api::LiveTransactions::IsUnspentReply);

    parser = Streaming::MessageParser(m.body());
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::LiveTransactions::UnspentState) {
            QCOMPARE(parser.isBool(), true);
            QCOMPARE(parser.boolData(), true);
        }
    }
}

Streaming::ConstBuffer TestApiLive::generate100(int nodeId)
{
    Message m = waitForReply(nodeId, Message(Api::UtilService, Api::Util::CreateAddress), Api::Util::CreateAddressReply);
    Streaming::MessageParser parser(m.body());
    Streaming::ConstBuffer address;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::Util::BitcoinAddress) {
            address = parser.bytesDataBuffer();
            break;
        }
    }

    Streaming::MessageBuilder builder(Streaming::NoHeader);
    builder.add(Api::RegTest::BitcoinAddress, address);
    builder.add(Api::RegTest::Amount, 101);
    m = waitForReply(nodeId, builder.message(Api::RegTestService, Api::RegTest::GenerateBlock), Api::RegTest::GenerateBlockReply);
    Q_ASSERT(m.serviceId() == Api::RegTestService);
    return address;
}
