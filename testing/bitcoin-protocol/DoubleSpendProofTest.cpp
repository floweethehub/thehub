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

#include "DoubleSpendProofTest.h"

#include <DoubleSpendProof.h>
#include <DoubleSpendProofStorage.h>
#include <TransactionBuilder.h>
#include <keystore.h>
#include <amount.h>

#include <primitives/key.h>
#include <version.h>
#include <streaming/streams.h>

namespace {
    void createDoubleSpend(const Tx &in, int outIndex, const CKey &key, Tx &out1, Tx &out2)
    {
        auto out = in.output(outIndex);
        assert(out.outputValue >= 0);
        {
            TransactionBuilder builder;
            builder.appendInput(in.createHash(), 0);
            builder.pushInputSignature(key, out.outputScript, out.outputValue);
            builder.appendOutput(50 * COIN);
            CKey k;
            k.MakeNewKey();
            builder.pushOutputPay2Address(k.GetPubKey().GetID());
            out1 = builder.createTransaction();
        }
        {
            TransactionBuilder builder;
            builder.appendInput(in.createHash(), 0);
            builder.pushInputSignature(key, out.outputScript, out.outputValue);
            builder.appendOutput(50 * COIN);
            CKey k;
            k.MakeNewKey();
            builder.pushOutputPay2Address(k.GetPubKey().GetID());
            out2 = builder.createTransaction();
        }
    }
}

void DoubleSpendProofTest::basic()
{
    CKey key;
    key.MakeNewKey();
    std::vector<FastBlock> blocks = bv->appendChain(101, key, MockBlockValidation::FullOutScript);
    blocks.front().findTransactions();
    const Tx coinbase = blocks.front().transactions().at(0);

    Tx first, second;
    createDoubleSpend(coinbase, 0, key, first, second);

    DoubleSpendProof dsp = DoubleSpendProof::create(first, second);
    QVERIFY(!dsp.isEmpty());
    QCOMPARE(dsp.prevTxId(), coinbase.createHash());
    QCOMPARE(dsp.prevOutIndex(), 0);

    auto s1 = dsp.firstSpender();
    QCOMPARE(s1.lockTime, (uint32_t) 0);
    QCOMPARE(s1.txVersion, (uint32_t) 2);
    QCOMPARE(s1.outSequence, (uint32_t) 0xFFFFFFFF);
    QVERIFY(s1.pushData.size() == 1);
    QVERIFY(s1.pushData.front().size() >= 70);
    QCOMPARE(s1.pushData.front().back(), (uint8_t) 65);
    QVERIFY(!s1.hashOutputs.IsNull());
    QVERIFY(!s1.hashSequence.IsNull());
    QVERIFY(!s1.hashPrevOutputs.IsNull());

    auto s2 = dsp.secondSpender();
    QCOMPARE(s2.lockTime, (uint32_t) 0);
    QCOMPARE(s2.txVersion, (uint32_t) 2);
    QCOMPARE(s2.outSequence, (uint32_t) 0xFFFFFFFF);
    QVERIFY(s2.pushData.size() == 1);
    QVERIFY(s2.pushData.front().size() >= 70);
    QCOMPARE(s2.pushData.front().back(), (uint8_t) 65);
    QVERIFY(!s2.hashOutputs.IsNull());
    QVERIFY(!s2.hashSequence.IsNull());
    QVERIFY(!s2.hashPrevOutputs.IsNull());

    // Will fail on MissingTransaction because we didn't add anything to the mempool yet.
    QCOMPARE(dsp.validate(*bv->mempool()), DoubleSpendProof::MissingTransaction);

    // add one to the mempool.
    bv->mempool()->insertTx(first);
    QCOMPARE(dsp.validate(*bv->mempool()), DoubleSpendProof::Valid);
}

void DoubleSpendProofTest::mempool()
{
    CKey key;
    key.MakeNewKey();
    std::vector<FastBlock> blocks = bv->appendChain(101, key, MockBlockValidation::FullOutScript);
    blocks.front().findTransactions();
    const Tx coinbase = blocks.front().transactions().at(0);

    Tx first, second;
    createDoubleSpend(coinbase, 0, key, first, second);
    bv->mempool()->insertTx(first);

    auto future = bv->addTransaction(second);
    QVERIFY(future.valid());
    QCOMPARE(future.get(), "258: txn-mempool-conflict"); // wait until finished

    QVERIFY(bv->mempool()->doubleSpendProofStorage()->proof(1).isEmpty() == false);

    std::list<CTransaction> res;
    bv->mempool()->remove(first.createOldTransaction(), res, false);

    // after removing out mempool entry, the proof also goes away
    QVERIFY(bv->mempool()->doubleSpendProofStorage()->proof(1).isEmpty());
}

void DoubleSpendProofTest::proofOrder()
{
    CKey key;
    key.MakeNewKey();
    std::vector<FastBlock> blocks = bv->appendChain(101, key, MockBlockValidation::FullOutScript);
    blocks.front().findTransactions();
    const Tx coinbase = blocks.front().transactions().at(0);

    Tx first, second;
    createDoubleSpend(coinbase, 0, key, first, second);
    DoubleSpendProof dsp1 = DoubleSpendProof::create(first, second);
    DoubleSpendProof dsp2 = DoubleSpendProof::create(second, first);

    // now, however we process them, the result is the same.
    QCOMPARE(dsp1.firstSpender().pushData.front(), dsp2.firstSpender().pushData.front());
    QCOMPARE(dsp1.secondSpender().pushData.front(), dsp2.secondSpender().pushData.front());
}

void DoubleSpendProofTest::serialization()
{
    CKey key;
    key.MakeNewKey();
    std::vector<FastBlock> blocks = bv->appendChain(101, key, MockBlockValidation::FullOutScript);
    blocks.front().findTransactions();
    const Tx coinbase = blocks.front().transactions().at(0);

    Tx first, second;
    createDoubleSpend(coinbase, 0, key, first, second);
    DoubleSpendProof dsp1 = DoubleSpendProof::create(first, second);

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << dsp1;
    const std::vector<uint8_t> blob(stream.begin(), stream.end());
    // logFatal() << blob.size();

    DoubleSpendProof dsp2;
    CDataStream restore(blob, SER_NETWORK, PROTOCOL_VERSION);
    restore >> dsp2;

    QCOMPARE(dsp1.createHash(), dsp2.createHash());

    // check if the second one validates
    bv->mempool()->insertTx(second);
    QCOMPARE(dsp2.validate(*bv->mempool()), DoubleSpendProof::Valid);
}

void DoubleSpendProofTest::testStupidUsage()
{
    CKey key;
    key.MakeNewKey();
    auto blocks = bv->appendChain(5, key, MockBlockValidation::FullOutScript);
    blocks[3].findTransactions();
    auto tx = blocks[3].transactions().at(0);
    blocks[4].findTransactions();

    try {
        auto tx2 = blocks[4].transactions().at(0);
        // coinbases can't be double spent (since they don't spent an output).
        DoubleSpendProof::create(tx, tx2);
        QFAIL("Coinbases can't be used to create a DSP");
    } catch (const std::runtime_error &e) { /* ok */ }

    // new Tx that spends a coinbase.
    TransactionBuilder builder;
    builder.appendInput(tx.createHash(), 0);
    auto out = tx.output(0);
    builder.pushInputSignature(key, out.outputScript, out.outputValue);
    builder.appendOutput(50 * COIN);
    tx = builder.createTransaction();

    try {
        DoubleSpendProof::create(tx, tx);
        QFAIL("Wrong type of input should throw");
    } catch (const std::runtime_error &e) { /* ok */ }
}

void DoubleSpendProofTest::bigTx()
{
    CKey key;
    key.MakeNewKey();
    std::vector<FastBlock> blocks = bv->appendChain(702, key, MockBlockValidation::FullOutScript);

    TransactionBuilder builder;
    for (size_t i = 0; i < 300; ++i) {
        auto block = blocks.at(i);
        block.findTransactions();
        QCOMPARE(block.transactions().size(), 1ul);
        auto tx = block.transactions().at(0);
        builder.appendInput(tx.createHash(), 0);
        auto out = tx.output(0);
        builder.pushInputSignature(key, out.outputScript, out.outputValue);
        builder.appendOutput(50 * COIN);
    }
    builder.pushOutputPay2Address(key.GetPubKey().GetID());
    auto first = builder.createTransaction();

    TransactionBuilder builder2;
    for (size_t i = 599; i >= 300; --i) {
        auto block = blocks.at(i);
        block.findTransactions();
        QCOMPARE(block.transactions().size(), 1ul);
        auto tx = block.transactions().at(0);
        builder2.appendInput(tx.createHash(), 0);
        auto out = tx.output(0);
        builder2.pushInputSignature(key, out.outputScript, out.outputValue);
        builder2.appendOutput(50 * COIN);
    }

    builder2.pushOutputPay2Address(key.GetPubKey().GetID());
    auto second = builder2.createTransaction();


    QBENCHMARK {
        DoubleSpendProof::create(first, second);
    }
}
