/*
 * This file is part of the Flowee project
 * Copyright (C) 2018-2019 Tom Zander <tomz@freedommail.ch>
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

#include "double_spend.h"

#include <primitives/FastTransaction.h>

#include <base58.h>
#include <script/sign.h>
#include <keystore.h>
#include <validationinterface.h>
#include <utilstrencodings.h>

class TestValidation : public ValidationInterface {
public:
    void DoubleSpendFound(const Tx &first_, const Tx &duplicate_) override {
        first = first_;
        duplicate = duplicate_;
    }
    Tx first, duplicate;

    void clear() {
        first = Tx();
        duplicate = Tx();
    }
};

void TestDoubleSpend::test()
{
    TestValidation myValidatioInterface;
    ValidationNotifier().addListener(&myValidatioInterface);
    QVERIFY(!myValidatioInterface.first.isValid());
    QVERIFY(!myValidatioInterface.duplicate.isValid());

    CKey key;
    std::vector<FastBlock> blocks = bv->appendChain(101, key);

    CBasicKeyStore keystore;
    keystore.AddKey(key);
    QCOMPARE(blocks.size(), 101ul);
    blocks.front().findTransactions();
    QCOMPARE(blocks.front().transactions().size(), 1ul);
    Tx coinbase = blocks.front().transactions().at(0);
    QVERIFY(coinbase.isValid());

    CMutableTransaction mutableFirst;
    mutableFirst.vin.resize(1);
    mutableFirst.vin[0].prevout.n = 0;
    mutableFirst.vin[0].prevout.hash = coinbase.createHash();
    mutableFirst.vout.resize(1);
    mutableFirst.vout[0].nValue = 50 * COIN;
    mutableFirst.vout[0].scriptPubKey << ToByteVector(key.GetPubKey()) << OP_CHECKSIG;
    SignSignature(keystore, coinbase.createOldTransaction(), mutableFirst, 0, SIGHASH_ALL | SIGHASH_FORKID);
    Tx first = Tx::fromOldTransaction(mutableFirst);
    auto future = bv->addTransaction(first);
    std::string result = future.get();
    QCOMPARE(result, std::string());
    QVERIFY(!myValidatioInterface.first.isValid());
    QVERIFY(!myValidatioInterface.duplicate.isValid());


    // now create a double-spending transaction.
    CMutableTransaction mutableDuplicate;
    mutableDuplicate.vin.resize(1);
    mutableDuplicate.vin[0].prevout.n = 0;
    mutableDuplicate.vin[0].prevout.hash = coinbase.createHash();
    mutableDuplicate.vout.resize(1);
    mutableDuplicate.vout[0].nValue = 49 * COIN;
    mutableDuplicate.vout[0].scriptPubKey << ToByteVector(key.GetPubKey()) << OP_CHECKSIG;
    SignSignature(keystore, coinbase.createOldTransaction(), mutableDuplicate, 0, SIGHASH_ALL | SIGHASH_FORKID);
    Tx duplicate = Tx::fromOldTransaction(mutableDuplicate);
    future = bv->addTransaction(duplicate);
    result = future.get();
    QCOMPARE(result, std::string("258: txn-mempool-conflict"));
    QVERIFY(myValidatioInterface.first.isValid());
    QCOMPARE(HexStr(myValidatioInterface.first.createHash()), HexStr(first.createHash()));
    QCOMPARE(HexStr(myValidatioInterface.duplicate.createHash()), HexStr(duplicate.createHash()));
}

QTEST_MAIN(TestDoubleSpend)
