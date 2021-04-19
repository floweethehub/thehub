/*
 * This file is part of the Flowee project
 * Copyright (C) 2017,2019 Tom Zander <tomz@freedommail.ch>
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
#include "test/test_bitcoin.h"

#include <boost/test/auto_unit_test.hpp>
#include <Application.h>
#include <chain.h>
#include <chainparams.h>
#include <main.h>
#include <script/interpreter.h>
#include <consensus/validation.h>

BOOST_FIXTURE_TEST_SUITE(UAHF, TestingSetup)

BOOST_AUTO_TEST_CASE(Test_transactionAcceptance)
{
    // Generate a 101-block chain:
    CKey coinbaseKey;
    std::vector<FastBlock> blocks = bv.appendChain(101, coinbaseKey, MockBlockValidation::StandardOutScript);
    const CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    FastBlock first = blocks[0];
    first.findTransactions();
    Tx coinbase1 = first.transactions().front();
    const uint256 hash0 = coinbase1.createHash();
    FastBlock second = blocks[1];
    second.findTransactions();
    Tx coinbase2 = second.transactions().front();
    const uint256 hash1 = coinbase2.createHash();

    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = hash1;
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 50*COIN;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    // build proper transaction, properly signed
    uint256 newHash = SignatureHash(scriptPubKey, tx, 0, 50 * COIN,
            SIGHASH_ALL | SIGHASH_FORKID, SCRIPT_ENABLE_SIGHASH_FORKID);
    std::vector<unsigned char> vchSig;
    bool ok = coinbaseKey.signECDSA(newHash, vchSig);
    BOOST_CHECK(ok);
    vchSig.push_back((unsigned char)SIGHASH_ALL | SIGHASH_FORKID);
    tx.vin[0].scriptSig << vchSig;
    { // Check if this will be acceptable to the mempool
        fRequireStandard = false;
        auto future = bv.addTransaction(Tx::fromOldTransaction(tx));
        std::string error = future.get();
        if (!error.empty())
            logDebug() << "::" << error;
        BOOST_CHECK(error.empty());
    }

    // next transaction, without FORKID
    tx.vin[0].prevout.hash = hash0;
    newHash = SignatureHash(scriptPubKey, tx, 0, 50 * COIN, SIGHASH_ALL);
    vchSig.clear();
    ok = coinbaseKey.signECDSA(newHash, vchSig);
    BOOST_CHECK(ok);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    tx.vin[0].scriptSig << vchSig;
    { // Check if this will be acceptable to the mempool
        auto future = bv.addTransaction(Tx::fromOldTransaction(tx));
        std::string error = future.get();
        if (error.empty())
            logDebug() << "::" << error;
        BOOST_CHECK(!error.empty());
    }
}

BOOST_AUTO_TEST_SUITE_END()
