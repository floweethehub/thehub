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

#include "TestLive.h"

#include <Message.h>

#include <streaming/BufferPool.h>
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

#include <primitives/FastTransaction.h>

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
    bool seenOutIndex = false;
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
        } else if (parser.tag() == Api::LiveTransactions::OutIndex) {
            QCOMPARE(index, 2);
            QVERIFY(parser.isInt());
            QCOMPARE(parser.intData(), 0);
            seenOutIndex = true;
        } else {
            logFatal() << "tag that doesn't belong:" << parser.tag();
            QVERIFY(false);
        }
    }
    QCOMPARE(index, 2);
    QCOMPARE(seenBlockHeight, true);
    QCOMPARE(seenOffsetInBlock, true);
    QCOMPARE(seenOutIndex, true);

    request.setMessageId(Api::LiveTransactions::GetUnspentOutput);
    con[0].send(request);
    m = waitForReply(0, request, Api::LiveTransactions::GetUnspentOutputReply);
    index = index2 = 0;
    parser = Streaming::MessageParser(m.body());
    seenBlockHeight = false;
    seenOffsetInBlock = false;
    seenOutIndex = false;
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
        } else if (parser.tag() == Api::LiveTransactions::OutIndex) {
            QCOMPARE(index, 2);
            QVERIFY(parser.isInt());
            QCOMPARE(parser.intData(), 0);
            seenOutIndex = true;
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
    QCOMPARE(seenOutIndex, true);


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

void TestApiLive::testGetMempoolInfo()
{
    startHubs(1);
    feedDefaultBlocksToHub(0);

    const Message request(Api::LiveTransactionService, Api::LiveTransactions::GetMempoolInfo);
    Message m = waitForReply(0, request,
             Api::LiveTransactionService, Api::LiveTransactions::GetMempoolInfoReply);

    Streaming::MessageParser parser(m.body());
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::LiveTransactions::MempoolSize) {
            QVERIFY(parser.isLong());
            QCOMPARE(parser.intData(), 0);
        }
        if (parser.tag() == Api::LiveTransactions::MempoolBytes) {
            QVERIFY(parser.isLong());
            QCOMPARE(parser.intData(), 0);
        }
        if (parser.tag() == Api::LiveTransactions::MempoolUsage) {
            QVERIFY(parser.isLong());
            QCOMPARE(parser.intData(), 0);
        }
        if (parser.tag() == Api::LiveTransactions::MaxMempool) {
            QVERIFY(parser.isLong());
        }
    }

    // known valid transaction on this chain.
    Streaming::BufferPool pool;
    pool.writeHex("0x01000000010b9d14b709aa59bd594edca17db2951c6660ebc8daa31ceae233a5550314f158000000006b483045022100b34a120e69bc933ae16c10db0f565cb2da1b80a9695a51707e8a80c9aa5c22bf02206c390cb328763ab9ab2d45f874d308af2837d6d8cfc618af76744b9eeb69c3934121022708a547a1d14ba6df79ec0f4216eeec65808cf0a32f09ad1cf730b44e8e14a6ffffffff01faa7be00000000001976a9148438266ad57aa9d9160e99a046e39027e4fb6b2a88ac00000000");
    Tx tx1(pool.commit());
    Streaming::MessageBuilder builder(pool);
    builder.add(Api::LiveTransactions::Transaction, tx1.data());

    // Send it.
    m = waitForReply(0, builder.message(Api::LiveTransactionService, Api::LiveTransactions::SendTransaction),
                             Api::LiveTransactionService, Api::LiveTransactions::SendTransactionReply);

    // it got accepted.
    QCOMPARE(m.serviceId(), Api::LiveTransactionService);
    // ask again.
    m = waitForReply(0, request,
             Api::LiveTransactionService, Api::LiveTransactions::GetMempoolInfoReply);

    bool seenMempoolSize = false;
    bool seenMempoolBytes = false;
    bool seenMempoolUsage = false;
    bool seenMax = false;
    Streaming::MessageParser parser2(m.body());
    while (parser2.next() == Streaming::FoundTag) {
        if (parser2.tag() == Api::LiveTransactions::MempoolSize) {
            seenMempoolSize = true;
            QVERIFY(parser2.isLong());
            QCOMPARE(parser2.intData(), 1);
        }
        if (parser2.tag() == Api::LiveTransactions::MempoolBytes) {
            seenMempoolBytes = true;
            QVERIFY(parser2.isLong());
            QCOMPARE(parser2.intData(), 192);
        }
        if (parser2.tag() == Api::LiveTransactions::MempoolUsage) {
            seenMempoolUsage = true;
            QVERIFY(parser2.isLong());
            QVERIFY(parser2.intData() > 192); // don't know exact size, this includes overhead
        }
        if (parser2.tag() == Api::LiveTransactions::MaxMempool) {
            seenMax = true;
            QVERIFY(parser2.isLong());
            QVERIFY(parser2.intData() >= 300000000); // 300MB, or more.
        }
        if (parser2.tag() == Api::LiveTransactions::MempoolMinFee) {
            QVERIFY(parser2.isDouble());
        }
    }
    QVERIFY(seenMempoolSize);
    QVERIFY(seenMempoolBytes);
    QVERIFY(seenMempoolUsage);
    QVERIFY(seenMax);
}

void TestApiLive::testGetTransaction()
{
    startHubs();
    feedDefaultBlocksToHub(0);

    Streaming::MessageBuilder builder(Streaming::NoHeader);
    builder.add(Api::BlockChain::BlockHeight, 112);
    builder.add(Api::BlockChain::Include_TxId, true);
    Message m = waitForReply(0, builder.message(Api::BlockChainService, Api::BlockChain::GetBlock), Api::BlockChain::GetBlockReply);
    uint256 txid;
    Streaming::MessageParser parser(m.body());
    bool isCoinbase = true;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::BlockChain::TxId) {
            if (isCoinbase) {
                isCoinbase = false;
                continue;
            }
            QVERIFY(parser.isByteArray());
            QCOMPARE(parser.dataLength(), 32);
            txid = parser.uint256Data();
            break;
        }
    }
    QVERIFY(!txid.IsNull());

    builder.add(Api::LiveTransactions::TxId, txid);
    m = waitForReply(0, builder.message(Api::LiveTransactionService,
                                        Api::LiveTransactions::GetTransaction), Api::LiveTransactions::GetTransactionReply);
    QVERIFY(!m.body().isEmpty());
    QCOMPARE(m.serviceId(), Api::LiveTransactionService);
    QCOMPARE(m.messageId(), Api::LiveTransactions::GetTransactionReply);
    parser = Streaming::MessageParser(m.body());
    bool foundTx = false;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::LiveTransactions::BlockHeight) {
            QVERIFY(parser.isInt());
            QCOMPARE(parser.intData(), 112);
        }
        else if (parser.tag() == Api::GenericByteData) {
            QVERIFY(parser.isByteArray());
            QCOMPARE(parser.dataLength(), 838);
            foundTx = true;
        }
    }
    QVERIFY(foundTx);

    Streaming::BufferPool pool;
    pool.writeHex("0x01000000010b9d14b709aa59bd594edca17db2951c6660ebc8daa31ceae233a5550314f158000000006b483045022100b34a120e69bc933ae16c10db0f565cb2da1b80a9695a51707e8a80c9aa5c22bf02206c390cb328763ab9ab2d45f874d308af2837d6d8cfc618af76744b9eeb69c3934121022708a547a1d14ba6df79ec0f4216eeec65808cf0a32f09ad1cf730b44e8e14a6ffffffff01faa7be00000000001976a9148438266ad57aa9d9160e99a046e39027e4fb6b2a88ac00000000");
    Tx tx1(pool.commit());

    // submit to mempool
    builder.add(Api::LiveTransactions::Transaction, tx1.data());
    con[0].send(builder.message(Api::LiveTransactionService, Api::LiveTransactions::SendTransaction));

    // search mempool on address
    builder.add(Api::LiveTransactions::BitcoinScriptHashed,
                uint256S("7c3cb6eb855660b775bbe66e1c245beb405000cc1c5374771a474051685b6e33"));
    m = waitForReply(0, builder.message(Api::LiveTransactionService,
                                        Api::LiveTransactions::GetTransaction), Api::LiveTransactions::GetTransactionReply);

    Streaming::MessageParser::debugMessage(m);
    QCOMPARE(m.serviceId(), Api::LiveTransactionService);
    QCOMPARE(m.messageId(), Api::LiveTransactions::GetTransactionReply);
    foundTx = false;
    parser = Streaming::MessageParser(m.body());
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::LiveTransactions::BlockHeight) {
            QVERIFY(false); // should not be here.
        }
        else if (parser.tag() == Api::GenericByteData) {
            QVERIFY(parser.isByteArray());
            QCOMPARE(parser.dataLength(), tx1.size());
            Tx tx2(parser.bytesDataBuffer());
            logFatal() << tx1.createHash() << tx2.createHash();
            QVERIFY(tx1.createHash() == tx2.createHash());
            foundTx = true;
        }
    }
    QVERIFY(foundTx);
}

Streaming::ConstBuffer TestApiLive::generate100(int nodeId)
{
    Message m = waitForReply(nodeId, Message(Api::UtilService, Api::Util::CreateAddress), Api::Util::CreateAddressReply);
    Streaming::MessageParser parser(m.body());
    Streaming::ConstBuffer address;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::Util::BitcoinP2PKHAddress) {
            address = parser.bytesDataBuffer();
            break;
        }
    }

    Streaming::MessageBuilder builder(Streaming::NoHeader);
    builder.add(Api::RegTest::BitcoinP2PKHAddress, address);
    builder.add(Api::RegTest::Amount, 101);
    m = waitForReply(nodeId, builder.message(Api::RegTestService, Api::RegTest::GenerateBlock), Api::RegTest::GenerateBlockReply);
    Q_ASSERT(m.serviceId() == Api::RegTestService);
    return address;
}
