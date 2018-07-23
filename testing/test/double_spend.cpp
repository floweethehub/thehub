/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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
#include "test_bitcoin.h"

#include <base58.h>
#include <script/sign.h>
#include <keystore.h>
#include <validationinterface.h>
#include <utilstrencodings.h>

#include <boost/test/auto_unit_test.hpp>

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

BOOST_FIXTURE_TEST_SUITE(DoubleSpend, TestingSetup)

BOOST_AUTO_TEST_CASE(DoubleSpend)
{
    TestValidation myValidatioInterface;
    ValidationNotifier().addListener(&myValidatioInterface);
    BOOST_CHECK(!myValidatioInterface.first.isValid());
    BOOST_CHECK(!myValidatioInterface.duplicate.isValid());

    CKey key;
    std::vector<FastBlock> blocks = bv.appendChain(101, key);

    CBasicKeyStore keystore;
    keystore.AddKey(key);
    BOOST_CHECK_EQUAL(blocks.size(), 101);
    blocks.front().findTransactions();
    BOOST_CHECK_EQUAL(blocks.front().transactions().size(), 1);
    Tx coinbase = blocks.front().transactions().at(0);
    BOOST_CHECK(coinbase.isValid());

    CMutableTransaction mutableFirst;
    mutableFirst.vin.resize(1);
    mutableFirst.vin[0].prevout.n = 0;
    mutableFirst.vin[0].prevout.hash = coinbase.createHash();
    mutableFirst.vout.resize(1);
    mutableFirst.vout[0].nValue = 50 * COIN;
    mutableFirst.vout[0].scriptPubKey << ToByteVector(key.GetPubKey()) << OP_CHECKSIG;
    SignSignature(keystore, coinbase.createOldTransaction(), mutableFirst, 0, SIGHASH_ALL | SIGHASH_FORKID);
    Tx first = Tx::fromOldTransaction(mutableFirst);
    auto future = bv.addTransaction(first);
    std::string result = future.get();
    BOOST_CHECK_EQUAL(result, std::string());
    BOOST_CHECK(!myValidatioInterface.first.isValid());
    BOOST_CHECK(!myValidatioInterface.duplicate.isValid());


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
    future = bv.addTransaction(duplicate);
    result = future.get();
    BOOST_CHECK_EQUAL(result, "258: txn-mempool-conflict");
    BOOST_CHECK(myValidatioInterface.first.isValid());
    BOOST_CHECK_EQUAL(HexStr(myValidatioInterface.first.createHash()), HexStr(first.createHash()));
    BOOST_CHECK_EQUAL(HexStr(myValidatioInterface.duplicate.createHash()), HexStr(duplicate.createHash()));
}

BOOST_AUTO_TEST_SUITE_END()

