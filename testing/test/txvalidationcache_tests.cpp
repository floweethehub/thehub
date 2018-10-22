/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2015 The Bitcoin Core developers
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

#include "test_bitcoin.h"
#include "consensus/validation.h"
#include "main.h"
#include "script/standard.h"
#include <primitives/FastBlock.h>
#include <key.h>
#include <consensus/consensus.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(tx_validationcache_tests)

#ifndef WIN32 // Avoid irrelevant fail due to database handles still being open at exit
//
// Testing fixture that pre-creates a
// 100-block REGTEST-mode block chain
//
class TestChain100Setup : public TestingSetup {
public:
    TestChain100Setup() {
        // Generate a 100-block chain:
        coinbaseKey.MakeNewKey(true);
        CScript scriptPubKey = CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
        CBlockIndex *parent = bv.blockchain()->Tip();
        CBlockIndex dummy;
        dummy.nTime = parent->nTime;
        dummy.phashBlock = parent->phashBlock;
        uint256 dummySha;
        uint32_t bits = parent->nBits;

        // std::vector<FastBlock> answer;
        for (int i = 0; i < COINBASE_MATURITY; ++i) {
            dummy.nHeight = parent->nHeight + i;
            dummy.nTime += 10;
            dummy.nBits = bits;
            FastBlock block = bv.createBlock(&dummy, scriptPubKey);
            bits = block.bits();
            bv.addBlock(block, Validation::SaveGoodToDisk, 0);
            coinbaseTxns.push_back(block.createOldBlock().vtx[0]);
            dummySha = block.createHash();
            dummy.phashBlock = &dummySha;
        }
        bv.waitValidationFinished();
    }

    // Create a new block with just given transactions, coinbase paying to
    // scriptPubKey, and try to add it to the current chain.
    CBlock createAndProcessBlock(const std::vector<CMutableTransaction>& txns,
                                 const CScript& scriptPubKey) {
        std::vector<CTransaction> tx;
        for (auto t : txns) { tx.push_back(t); }
        bv.waitValidationFinished(); // make sure that Tip really is Tip
        FastBlock block = bv.createBlock(bv.blockchain()->Tip(), scriptPubKey, tx);
        auto future = bv.addBlock(block, Validation::SaveGoodToDisk, 0).start();
        future.waitUntilFinished();
        return block.createOldBlock();
    }

    void ToMemPool(CMutableTransaction& tx, bool expectPass)
    {
        auto future = bv.addTransaction(Tx::fromOldTransaction(tx));
        std::string result = future.get();
        if (expectPass != result.empty()) {
            logCritical() << "ToMemPool" << result;
            throw std::runtime_error(expectPass ? result : "ToMemPool gave no error");
        }
    }


    std::vector<CTransaction> coinbaseTxns; // For convenience, coinbase transactions
    CKey coinbaseKey; // private/public key needed to spend coinbase transactions
};

BOOST_FIXTURE_TEST_CASE(tx_mempool_block_doublespend, TestChain100Setup)
{
    BOOST_CHECK(true);
    // Make sure skipping validation of transctions that were
    // validated going into the memory pool does not allow
    // double-spends in blocks to pass validation when they should not.

    CScript scriptPubKey = CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    // Create a double-spend of mature coinbase txn:
    std::vector<CMutableTransaction> spends;
    spends.resize(2);
    for (int i = 0; i < 2; i++)
    {
        spends[i].vin.resize(1);
        spends[i].vin[0].prevout.hash = coinbaseTxns[0].GetHash();
        spends[i].vin[0].prevout.n = 0;
        spends[i].vout.resize(1);
        spends[i].vout[0].nValue = 11*CENT;
        spends[i].vout[0].scriptPubKey = scriptPubKey;

        // Sign:
        std::vector<unsigned char> vchSig;
        uint256 hash = SignatureHash(scriptPubKey, spends[i], 0, 50 * COIN, SIGHASH_ALL | SIGHASH_FORKID, SCRIPT_ENABLE_SIGHASH_FORKID);
        BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
        vchSig.push_back((unsigned char)SIGHASH_ALL + SIGHASH_FORKID);
        spends[i].vin[0].scriptSig << vchSig;
    }

    CBlock block;
    // block with both of those transactions should be rejected.
    block = createAndProcessBlock(spends, scriptPubKey);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());

    // Sanity test: first spend in mempool, second in block, that's OK:
    std::vector<CMutableTransaction> oneSpend;
    oneSpend.push_back(spends[0]);
    bv.mp.clear();
    ToMemPool(spends[1], /* expectPass = */ true);
    block = createAndProcessBlock(oneSpend, scriptPubKey);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() == block.GetHash());
    // spends[1] should have been removed from the mempool when the
    // block with spends[0] is accepted:
    BOOST_CHECK_EQUAL(bv.mp.size(), 0);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
