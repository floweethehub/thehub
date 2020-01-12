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
    QCOMPARE(s1.txVersion, (uint32_t) 1);
    QCOMPARE(s1.outSequence, (uint32_t) 0xFFFFFFFF);
    QVERIFY(s1.pushData.size() == 1);
    QVERIFY(s1.pushData.front().size() >= 70);
    QCOMPARE(s1.pushData.front().back(), (uint8_t) 65);
    QVERIFY(!s1.hashOutputs.IsNull());
    QVERIFY(!s1.hashSequence.IsNull());
    QVERIFY(!s1.hashPrevOutputs.IsNull());

    auto s2 = dsp.doubleSpender();
    QCOMPARE(s2.lockTime, (uint32_t) 0);
    QCOMPARE(s2.txVersion, (uint32_t) 1);
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
    QCOMPARE(dsp1.doubleSpender().pushData.front(), dsp2.doubleSpender().pushData.front());
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
