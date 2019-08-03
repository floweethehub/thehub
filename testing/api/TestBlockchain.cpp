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

#include "TestBlockchain.h"

#include <Message.h>
#include <utilstrencodings.h>

#include <streaming/BufferPool.h>
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

void TestApiBlockchain::testChainInfo()
{
    startHubs();
    Message m = waitForReply(0, Message(Api::BlockChainService, Api::BlockChain::GetBlockChainInfo),
                             Api::BlockChain::GetBlockChainInfoReply);
    QCOMPARE(m.serviceId(), (int) Api::BlockChainService);
    QCOMPARE(m.messageId(), (int) Api::BlockChain::GetBlockChainInfoReply);
    Streaming::MessageParser parser(m.body());
    bool seenTitle = false;
    while(parser.next() == Streaming::FoundTag) {
        if (parser.tag() == 68) {
            seenTitle = true;
            QVERIFY(parser.isString());
            QCOMPARE(parser.stringData(), std::string("regtest"));
        }
        else if (parser.tag() == 69) { // block-height
            QVERIFY(parser.isInt());
            QCOMPARE(parser.intData(), 0);
        }
        else if (parser.tag() == 70) { // header-height
            QVERIFY(parser.isInt());
            QCOMPARE(parser.intData(), 0);
        }
        else if (parser.tag() == 71) { // last blockhash
            QVERIFY(parser.isByteArray());
            QCOMPARE(parser.dataLength(), 32);
            QByteArray parsedData(parser.bytesData().data(), 32);
            QCOMPARE(parsedData, QByteArray::fromHex("06226E46111A0B59CAAF126043EB5BBF28C34F3A5E332A1FC7B2B73CF188910F"));
        }
        else if (parser.tag() == 65) { // difficulty
            QVERIFY(parser.isDouble());
        }
        else if (parser.tag() == 66) { // time
            QVERIFY(parser.isLong());
            QCOMPARE(parser.longData(), (uint64_t) 1296688602);
        }
        else if (parser.tag() == 72) { // progress
            QVERIFY(parser.isDouble());
            QCOMPARE(parser.doubleData(), 1.);
        }
        else if (parser.tag() == 67) { // chain work
            QVERIFY(parser.isByteArray());
            QCOMPARE(parser.dataLength(), 32);
            QVERIFY(parser.uint256Data() == uint256S("0000000000000000000000000000000000000000000000000000000000000000"));
        }
    }
}

void TestApiBlockchain::testGetTransaction()
{
    startHubs();
    feedDefaultBlocksToHub(0);

    Streaming::BufferPool pool;
    Streaming::MessageBuilder builder(pool);
    builder.add(Api::BlockChain::BlockHeight, 112);
    builder.add(Api::BlockChain::Tx_OffsetInBlock, 1019);

    Message m = waitForReply(0, builder.message(Api::BlockChainService, Api::BlockChain::GetTransaction), Api::BlockChain::GetTransactionReply);
    QCOMPARE(m.serviceId(), (int) Api::BlockChainService);
    QCOMPARE(m.body().size(), 841); // the whole, raw, transaction plus 3 bytes overhead

    builder.add(Api::BlockChain::BlockHeight, 112);
    builder.add(Api::BlockChain::Tx_OffsetInBlock, 1019);
    builder.add(Api::BlockChain::Include_TxId, true);
    m = waitForReply(0, builder.message(Api::BlockChainService, Api::BlockChain::GetTransaction), Api::BlockChain::GetTransactionReply);
    QCOMPARE(m.serviceId(), (int) Api::BlockChainService);
    Streaming::MessageParser p(m.body());
    p.next();
    QCOMPARE(p.uint256Data(), uint256S("0xe455fc2cb76d11a015fe120c18cb590203b6a217640afcf7b3be898db7527a44"));
    QCOMPARE(p.next(), Streaming::EndOfDocument);


    builder.add(Api::BlockChain::BlockHeight, 112);
    builder.add(Api::BlockChain::Tx_OffsetInBlock, 1019);
    builder.add(Api::BlockChain::Include_Inputs, true);
    m = waitForReply(0, builder.message(Api::BlockChainService, Api::BlockChain::GetTransaction), Api::BlockChain::GetTransactionReply);
    p = Streaming::MessageParser(m.body());
    p.next();
    QCOMPARE(p.tag(), (uint32_t) 9);
    QCOMPARE(p.uint256Data(), uint256S("0x5256291727342b4cbd0d09bb09c745f4054d40618d19d2c037c9143d9e7399a4"));
    p.next();
    QCOMPARE(p.tag(), (uint32_t) 10);
    QCOMPARE(p.intData(), 0);
    p.next();
    QCOMPARE(p.tag(), (uint32_t) 11);
    QCOMPARE(p.dataLength(), 107);
    QCOMPARE(p.next(), Streaming::EndOfDocument);

    builder.add(Api::BlockChain::BlockHeight, 112);
    builder.add(Api::BlockChain::Tx_OffsetInBlock, 1019);
    builder.add(Api::BlockChain::Include_OutputAmounts, true);
    m = waitForReply(0, builder.message(Api::BlockChainService, Api::BlockChain::GetTransaction), Api::BlockChain::GetTransactionReply);
    p = Streaming::MessageParser(m.body());
    for (int i = 0; i < 20; ++i) {
        QCOMPARE(p.next(), Streaming::FoundTag);
        QCOMPARE(p.tag(), (uint32_t) Api::BlockChain::Tx_Out_Index);
        QCOMPARE(p.intData(), i);
        QCOMPARE(p.next(), Streaming::FoundTag);
        QCOMPARE(p.tag(), (uint32_t) Api::Amount);
        QCOMPARE(p.longData(), (uint64_t) 249999850);
    }
    QCOMPARE(p.next(), Streaming::EndOfDocument);

    builder.add(Api::BlockChain::BlockHeight, 112);
    builder.add(Api::BlockChain::Tx_OffsetInBlock, 1019);
    builder.add(Api::BlockChain::Include_OutputAmounts, true);
    builder.add(Api::BlockChain::FilterOutputIndex, 1);
    m = waitForReply(0, builder.message(Api::BlockChainService, Api::BlockChain::GetTransaction), Api::BlockChain::GetTransactionReply);
    p = Streaming::MessageParser(m.body());
    QCOMPARE(p.next(), Streaming::FoundTag);
    QCOMPARE(p.tag(), (uint32_t) Api::BlockChain::Tx_Out_Index);
    QCOMPARE(p.intData(), 1);
    QCOMPARE(p.next(), Streaming::FoundTag);
    QCOMPARE(p.tag(), (uint32_t) Api::Amount);
    QCOMPARE(p.longData(), (uint64_t) 249999850);
    QCOMPARE(p.next(), Streaming::EndOfDocument);
}
