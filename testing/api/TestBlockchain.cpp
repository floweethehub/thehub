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
    con[0].send(Message(Api::BlockChainService, Api::BlockChain::GetBlockChainInfo));
    Message m = waitForMessage(0, Api::BlockChainService, Api::BlockChain::GetBlockChainInfoReply, Api::BlockChain::GetBlockChainInfo);

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
