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

#include "TestBlockchain.h"

#include <Message.h>
#include <TransactionBuilder.h>
#include <sha256.h>
#include <utilstrencodings.h>

#include <streaming/BufferPool.h>
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

#include <primitives/FastTransaction.h>
#include <primitives/transaction.h>

void TestApiBlockchain::testChainInfo()
{
    startHubs();
    Message m = waitForReply(0, Message(Api::BlockChainService, Api::BlockChain::GetBlockChainInfo),
                             Api::BlockChain::GetBlockChainInfoReply);
    QCOMPARE(m.serviceId(), (int) Api::BlockChainService);
    QCOMPARE(m.messageId(), (int) Api::BlockChain::GetBlockChainInfoReply);
    Streaming::MessageParser parser(m.body());
    bool seenTitle = false;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == 67) {
            seenTitle = true;
            QVERIFY(parser.isString());
            QCOMPARE(parser.stringData(), std::string("regtest"));
        }
        else if (parser.tag() == 68) { // block-height
            QVERIFY(parser.isInt());
            QCOMPARE(parser.intData(), 0);
        }
        else if (parser.tag() == 69) { // header-height
            QVERIFY(parser.isInt());
            QCOMPARE(parser.intData(), 0);
        }
        else if (parser.tag() == 70) { // last blockhash
            QVERIFY(parser.isByteArray());
            QCOMPARE(parser.dataLength(), 32);
            QByteArray parsedData(parser.bytesData().data(), 32);
            QCOMPARE(parsedData, QByteArray::fromHex("06226E46111A0B59CAAF126043EB5BBF28C34F3A5E332A1FC7B2B73CF188910F"));
        }
        else if (parser.tag() == 64) { // difficulty
            QVERIFY(parser.isDouble());
        }
        else if (parser.tag() == 63) { // time
            QVERIFY(parser.isLong());
            QCOMPARE(parser.longData(), (uint64_t) 1296688602);
        }
        else if (parser.tag() == 71) { // progress
            QVERIFY(parser.isDouble());
            QCOMPARE(parser.doubleData(), 1.);
        }
        else if (parser.tag() == 66) { // chain work
            QVERIFY(parser.isByteArray());
            QCOMPARE(parser.dataLength(), 32);
            QVERIFY(parser.uint256Data() == uint256S("0000000000000000000000000000000000000000000000000000000000000000"));
        }
    }
    QVERIFY(seenTitle);
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
    QCOMPARE(p.tag(), (uint32_t) 20);
    QCOMPARE(p.uint256Data(), uint256S("0x5256291727342b4cbd0d09bb09c745f4054d40618d19d2c037c9143d9e7399a4"));
    p.next();
    QCOMPARE(p.tag(), (uint32_t) 21);
    QCOMPARE(p.intData(), 0);
    p.next();
    QCOMPARE(p.tag(), (uint32_t) 22);
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

void TestApiBlockchain::testGetScript()
{
    startHubs();
    feedDefaultBlocksToHub(0);

    Streaming::BufferPool pool;
    Streaming::MessageBuilder builder(pool);
    builder.add(Api::BlockChain::BlockHeight, 112);
    builder.add(Api::BlockChain::Tx_OffsetInBlock, 1019);
    // Include_Outputs only gets the unmodified, actual data from the outputs.
    // This currently means the Amount and the Script, and the index
    // Nothing else should be sent
    builder.add(Api::BlockChain::Include_Outputs, true);

    auto m = waitForReply(0, builder.message(Api::BlockChainService, Api::BlockChain::GetTransaction), Api::BlockChain::GetTransactionReply);
    QCOMPARE(m.serviceId(), (int) Api::BlockChainService);
    Streaming::MessageParser p(m.body());
    QVector<Streaming::ConstBuffer> scripts;
    while (p.next() == Streaming::FoundTag) {
        const auto tag = p.tag();
        QVERIFY(tag == Api::BlockChain::Tx_OutputScript
                || tag == Api::BlockChain::Tx_Out_Amount
                || tag == Api::BlockChain::Tx_Out_Index);
        if (tag == Api::BlockChain::Tx_OutputScript) // remember for a next test
            scripts.append(p.bytesDataBuffer());
    }

    builder.add(Api::BlockChain::BlockHeight, 112);
    builder.add(Api::BlockChain::Tx_OffsetInBlock, 1019);
    // Include_OutputsAddressHash hashes the output script (once) and includes the hash.
    // This can be used as a unique ID instead of addresses, of which there are too many types.
    builder.add(Api::BlockChain::Include_OutputScriptHash, true);
    m = waitForReply(0, builder.message(Api::BlockChainService, Api::BlockChain::GetTransaction), Api::BlockChain::GetTransactionReply);
    QCOMPARE(m.serviceId(), (int) Api::BlockChainService);

    p = Streaming::MessageParser(m.body());
    int index = -1;
    while (p.next() == Streaming::FoundTag) {
        const auto tag = p.tag();
        QVERIFY(tag == Api::BlockChain::Tx_Out_ScriptHash
                || tag == Api::BlockChain::Tx_Out_Index);
        if (tag == Api::BlockChain::Tx_Out_Index) {
            QVERIFY(index == -1);
            QVERIFY(p.isInt());
            index = p.intData();
            QVERIFY(index >= 0);
            QVERIFY(index < scripts.size());
        }
        if (tag == Api::BlockChain::Tx_Out_ScriptHash) {
            QVERIFY(p.isByteArray());
            QCOMPARE(p.dataLength(), 32);
            QVERIFY(index >= 0);
            QVERIFY(index < scripts.size());

            CSHA256 hasher;
            const auto script = scripts.at(index);
            hasher.Write(script.begin(), script.size());
            char buf[32];
            hasher.Finalize(buf);
            QVERIFY(memcmp(buf, p.bytesDataBuffer().begin(), 32) == 0);
            if (index == 5) // lets at least hardcode one comparison. Another test should catch that, but wth
                QVERIFY(p.uint256Data() == uint256S("8CE6447F1046F208F00B68EDF06F3EE974395F795ABAF60732CB6B2B500D53FE"));

            index = -1;
        }
    }

    builder.add(Api::BlockChain::BlockHeight, 112);
    builder.add(Api::BlockChain::Tx_OffsetInBlock, 1019);
    // Include_OutputAddresses interprets the output script and returns something if it is a P2PKH address.
    builder.add(Api::BlockChain::Include_OutputAddresses, true);
    m = waitForReply(0, builder.message(Api::BlockChainService, Api::BlockChain::GetTransaction), Api::BlockChain::GetTransactionReply);
    QCOMPARE(m.serviceId(), (int) Api::BlockChainService);

    p = Streaming::MessageParser(m.body());
    while (p.next() == Streaming::FoundTag) {
        const auto tag = p.tag();
        QVERIFY(tag ==Api::BlockChain::Tx_Out_Address
                || tag == Api::BlockChain::Tx_Out_Index);
        if (tag == Api::BlockChain::Tx_Out_Address) {
            QVERIFY(p.isByteArray());
            QCOMPARE(p.dataLength(), 20);
        }
    }
}

void TestApiBlockchain::testFilterOnScriptHash()
{
    startHubs();
    feedDefaultBlocksToHub(0);

    Streaming::BufferPool pool;
    Streaming::MessageBuilder builder(pool);
    builder.add(Api::BlockChain::BlockHeight, 115);
    builder.add(Api::BlockChain::AddFilterScriptHash, uint256S("1111111111111111111111111111111111111111111111111111111111111111"));
    builder.add(Api::BlockChain::FullTransactionData, false);

    auto m = waitForReply(0, builder.message(Api::BlockChainService,
                                          Api::BlockChain::GetBlock), Api::BlockChain::GetBlockReply);

    // make sure that we return only transactions matching. Even if nothing matches
    Streaming::MessageParser p(m.body());
    QCOMPARE(p.next(), Streaming::FoundTag);
    QCOMPARE(p.tag(), (uint32_t) Api::BlockChain::BlockHeight);
    QCOMPARE(p.intData(), 115);
    QCOMPARE(p.next(), Streaming::FoundTag);
    QCOMPARE(p.tag(), (uint32_t) Api::BlockChain::BlockHash);
    QCOMPARE(p.dataLength(), 32);
    QCOMPARE(p.next(), Streaming::EndOfDocument);

    // now use a filter that hits approx 80% of the transactions.
    builder.add(Api::BlockChain::BlockHeight, 115);
    builder.add(Api::BlockChain::SetFilterScriptHash, uint256S("00a7a0e144e7050ef5622b098faf19026631401fa46e68a93fe5e5630b94dcea"));
    builder.add(Api::BlockChain::FullTransactionData, false);

    m = waitForReply(0, builder.message(Api::BlockChainService,
                                          Api::BlockChain::GetBlock), Api::BlockChain::GetBlockReply);

    p = Streaming::MessageParser(m.body());
    QCOMPARE(p.next(), Streaming::FoundTag);
    QCOMPARE(p.tag(), (uint32_t) Api::BlockChain::BlockHeight);
    QCOMPARE(p.intData(), 115);
    QCOMPARE(p.next(), Streaming::FoundTag);
    QCOMPARE(p.tag(), (uint32_t) Api::BlockChain::BlockHash);
    QCOMPARE(p.dataLength(), 32);
    // not the coinbase.
    std::deque<int> positions = {181, 1018, 1855, 2692, 3529, 4366, 5203, 6040, 6877, 7714, 8551,
                                 9388, 10225, 11063, 11901, 12739, 13577, 14415, 15253, 16091, 16929};
    for (int pos : positions) {
        QCOMPARE(p.next(), Streaming::FoundTag);
        QCOMPARE(p.tag(), (uint32_t) Api::BlockChain::Tx_OffsetInBlock);
        QCOMPARE(p.intData(), pos);
        QCOMPARE(p.next(), Streaming::FoundTag);
        QCOMPARE(p.tag(), (uint32_t) Api::BlockChain::Separator);
    }
    QCOMPARE(p.next(), Streaming::EndOfDocument);
}

void TestApiBlockchain::fetchTransaction()
{
    startHubs();
    feedDefaultBlocksToHub(0);

    Streaming::BufferPool pool;
    Streaming::MessageBuilder builder(pool);

    const int BlockSize = 17759;
    for (int i = -1; i < BlockSize + 10; ++i) {
        builder.add(Api::BlockChain::BlockHeight, 113);
        builder.add(Api::BlockChain::Tx_OffsetInBlock, i);
        auto m = waitForReply(0, builder.message(Api::BlockChainService,
                                          Api::BlockChain::GetTransaction), Api::BlockChain::GetTransactionReply);
        if (i > 100 && m.serviceId() == Api::APIService)
            break;
    }

    // block height out of range.
    builder.add(Api::BlockChain::BlockHeight, 200);
    builder.add(Api::BlockChain::Tx_OffsetInBlock, 81);
    auto m = waitForReply(0, builder.message(Api::BlockChainService,
                                      Api::BlockChain::GetTransaction), Api::BlockChain::GetTransactionReply);

    // negative block height
    builder.add(Api::BlockChain::BlockHeight, -10);
    builder.add(Api::BlockChain::Tx_OffsetInBlock, 81);
    m = waitForReply(0, builder.message(Api::BlockChainService,
                                      Api::BlockChain::GetTransaction), Api::BlockChain::GetTransactionReply);

    // and finish with a known good one.
    builder.add(Api::BlockChain::BlockHeight, 113);
    builder.add(Api::BlockChain::Tx_OffsetInBlock, 81);
    m = waitForReply(0, builder.message(Api::BlockChainService,
                                      Api::BlockChain::GetTransaction), Api::BlockChain::GetTransactionReply);
    QCOMPARE(m.messageId(), Api::BlockChain::GetTransactionReply);
}

void TestApiBlockchain::filterBlock()
{
    startHubs();
    feedDefaultBlocksToHub(0);

    Streaming::BufferPool pool;
    Streaming::MessageBuilder builder(pool);
    // pretty block with a nice op_return and a 3->1 checksig tx.
    const auto blockData = ParseHex("00000020b435cf812ef738b33c7869a56d2e2565d367ae706c46756db1661390393c714fbfaff736e0b5059c54cf4871cae0c57b2e9961f8246e4389217f9c2f6a843fc147505b60ffff7f20000000000302000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0f017401010545423132380400000000ffffffff014cf5052a010000001976a914ec9cc6eb660a07f74e3c785d622e9ca2d951044688ac00000000020000000383f8ea2fa00e72dac2772be53a9648a8ae2deefb276ec0382cb3b99fa7bc1f98020000006a473044022052e514bacbbace82678727ab7127f11e3a4fe890e7a12a7017191231436eabd702207082915ce32db61d199c4e902d5cf32ae9a538dac76799a4e8695576669ea6c5412103a5f3ef29279b67d5369383516e5010a468c685021eb48b0576bd3366fed91ce3ffffffff9290d0b912464a953fbba24315030a2672f99092d5ce9d8bca18ebc7585220d6020000006a47304402202e9c31adf0bf2610193749bead883aca74915eb132fdb5281136f746b71fc9a502202cd746aef5f6b832633406f0747b949832df8e4b9f96ce36d9210e9f28e0bd46412103a5f3ef29279b67d5369383516e5010a468c685021eb48b0576bd3366fed91ce3ffffffff447a52b78d89beb3f7fc0a6417a2b6030259cb180c12fe15a0116db72cfc55e4020000006b483045022100b4307f90a08eea0512405c15dd8fa0519604e6039d66527d8fb4b115072bdf38022077ad2af5d03af82e04b19fd321c0fa88a44307026db05a38b149c094cfa72f33412103a5f3ef29279b67d5369383516e5010a468c685021eb48b0576bd3366fed91ce3ffffffff01d813b42c000000001976a9147d7ea8e0ae26260d310d8f485a80ba37c128d21c88ac000000000200000002407e8798680f76b0b38597f08ab9520d6f4e4c520f73fc1a470eae469e2654c9070000006b483045022100faeb903384c7144c4354cd405d6443e3cbd589eb9eac19223e80c2ae10b5060e022012cedbd780b0d74cffdc74beecac12b5c89a8eb093542d3a865e24a29b4098e64121034abe888b6be024a55356215502a094d4f040bd5f216cc7c4ea6f924cdc0456cfffffffff4570a84bbf31e7e5fd2154a2c330938fc67605434760061371bae29a35b0950d100000006a4730440220331958a8abd7799274eed69a7b5136e61a2b3d0f212627b9b9b1ee06ca1cb4df022037bf04155dab616db91ea5441adf3ce762a9a9ebef85eaf38f20c89352ccb9e0412103dea00a05e04ee8637756a284b1d38c075bda7f722d496a187501f72c21595692ffffffff0200000000000000000a6a08198278900982309a9e757d01000000001976a9140d77abf49d3a286154d15e31b1a7bd9898c3566988ac00000000");
    QCOMPARE(Api::Mining::GenericByteData, 1);
    QCOMPARE(Api::Mining::SubmitBlock, 0);
    QCOMPARE(Api::Mining::SubmitBlockReply, 1);
    QCOMPARE(Api::Mining::BlockHash, 5);
    builder.add(Api::Mining::GenericByteData, blockData);
    auto m = waitForReply(0, builder.message(Api::MiningService, Api::Mining::SubmitBlock), Api::Mining::SubmitBlockReply);
    QCOMPARE(m.messageId(), Api::Mining::SubmitBlockReply);
    {
        Streaming::MessageParser  parser(m);
        bool checkedHash = false;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::Mining::BlockHash) {
                QVERIFY(parser.isByteArray());
                QCOMPARE(parser.dataLength(), 32);
                QVERIFY(parser.uint256Data() == uint256S("0f820cace2f02b8cc475bb46e03172cf4eb09874ad2ef1ef1e8c91ca62bdceef"));
                checkedHash = true;
            }
        }
        QVERIFY(checkedHash);
    }

    // filter on a script-type that does not exist in any of our transactions.
    QCOMPARE(Api::BlockChain::BlockHeight, 7);
    QCOMPARE(Api::BlockChain::FilterOnScriptType, 39);
    builder.add(Api::BlockChain::BlockHeight, 116);
    builder.add(Api::BlockChain::FilterOnScriptType, Api::ScriptTag::OpCheckmultisig);
    m = waitForReply(0, builder.message(Api::BlockChainService, Api::BlockChain::GetBlock), Api::BlockChain::GetBlockReply);
    {
        Streaming::MessageParser  parser(m);
        while (parser.next() == Streaming::FoundTag) {
            QVERIFY(parser.tag() != Api::Separator); // the above request should give us no results.
        }
    }

    // filter on op_return which should give us 2 transactions.
    QCOMPARE(Api::BlockChain::FilterOnScriptType, 39);
    QCOMPARE(Api::BlockChain::Tx_OffsetInBlock, 8);
    QCOMPARE(Api::BlockChain::FullTransactionData, 45);
    builder.add(Api::BlockChain::BlockHeight, 116);
    builder.add(Api::BlockChain::FilterOnScriptType, Api::ScriptTag::OpReturn);
    builder.add(Api::BlockChain::FullTransactionData, false);
    m = waitForReply(0, builder.message(Api::BlockChainService, Api::BlockChain::GetBlock), Api::BlockChain::GetBlockReply);
    {
        Streaming::MessageParser  parser(m);
        bool checkedTx = false;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::Tx_OffsetInBlock) {
                QCOMPARE(parser.intData(), 667);
                checkedTx = true;
            }
        }
        QVERIFY(checkedTx);
    }
}

QTEST_MAIN(TestApiBlockchain)
