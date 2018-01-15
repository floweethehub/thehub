/*
 * This file is part of the Flowee project
 * Copyright (C) 2017 Tom Zander <tomz@freedommail.ch>
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

BOOST_AUTO_TEST_CASE(Test_Enabling)
{
    BOOST_CHECK_EQUAL(Application::uahfChainState(), Application::UAHFActive);
    BOOST_CHECK_EQUAL(Application::uahfStartTime(), 1296688602);

    mapArgs["-uahf"] = "false";
    MockApplication::doInit();
    BOOST_CHECK_EQUAL(Application::uahfChainState(), Application::UAHFDisabled);
}

BOOST_AUTO_TEST_CASE(Test_isCommitment) {
    std::vector<unsigned char> data{};

    // Empty commitment.
    auto s = CScript() << OP_RETURN << data;
    BOOST_CHECK(s.isCommitment(data));

    // Commitment to a value of the wrong size.
    data.push_back(42);
    BOOST_CHECK(!s.isCommitment(data));

    // Not a commitment.
    s = CScript() << data;
    BOOST_CHECK(!s.isCommitment(data));

    // Non empty commitment.
    s = CScript() << OP_RETURN << data;
    BOOST_CHECK(s.isCommitment(data));

    // Commitment to the wrong value.
    data[0] = 0x42;
    BOOST_CHECK(!s.isCommitment(data));

    // Commitment to a larger value.
    std::string str = "Bitcoin: A peer-to-peer Electronic Cash System";
    data = std::vector<unsigned char>(str.begin(), str.end());
    BOOST_CHECK(!s.isCommitment(data));

    s = CScript() << OP_RETURN << data;
    BOOST_CHECK(s.isCommitment(data));

    // 64 bytes commitment, still valid.
    data.resize(64);
    s = CScript() << OP_RETURN << data;
    BOOST_CHECK(s.isCommitment(data));

    // Commitment is too large.
    data.push_back(23);
    s = CScript() << OP_RETURN << data;
    BOOST_CHECK(!s.isCommitment(data));

    // Check with the actual replay commitment we are going to use.
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params &params = Params().GetConsensus();
    s = CScript() << OP_RETURN << params.antiReplayOpReturnCommitment;
    BOOST_CHECK(s.isCommitment(params.antiReplayOpReturnCommitment));
}

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
    bool ok = coinbaseKey.Sign(newHash, vchSig);
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
    ok = coinbaseKey.Sign(newHash, vchSig);
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


// UAHF's rollback protection is turned off for regtest, as such we need to use mainnet to test it.
class MainTestingFixture : public TestingSetup
{
public:
    MainTestingFixture() : TestingSetup(CBaseChainParams::MAIN) {}
};

BOOST_FIXTURE_TEST_SUITE(UAHF2, MainTestingFixture)

BOOST_AUTO_TEST_CASE(Test_startWithBigBlock)
{
    BOOST_CHECK_EQUAL(Application::uahfChainState(), Application::UAHFWaiting);
    std::vector<FastBlock> blocks = bv.appendChain(20);
    MockApplication::setUAHFStartTime(bv.blockchain()->Tip()->GetMedianTimePast());

    CScript dummy;
    FastBlock block = bv.createBlock(bv.blockchain()->Tip(), dummy);
    auto future = bv.addBlock(block, 0);
    future.setCheckPoW(false);
    future.setCheckMerkleRoot(false);
    future.start();
    future.waitUntilFinished();
    BOOST_CHECK_EQUAL(future.error(), "bad-blk-too-small");
}

BOOST_AUTO_TEST_SUITE_END()
