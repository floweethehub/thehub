/*
 * This file is part of the flowee project
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
#include <boost/test/unit_test.hpp>

#include <validation/BlockValidation_p.h>
#include <server/BlocksDB.h>
#include <server/script/interpreter.h>
#include <server/script/standard.h>
#include <primitives/transaction.h>
#include <chainparams.h>
#include <primitives/key.h>
#include <consensus/merkle.h>
#include <main.h>

#include <boost/foreach.hpp>

namespace {
void nothing(){
    logDebug() << "nothing";
}
// as we know that headers and final block validation happen in the strand, this
// helper method may ensure we wait long enough to allow various actions to happen.
// it typically is Ok to have a higher count than required for internal details in the BV code.
void waitForStrand(MockBlockValidation &bv, int count = 10) {
    for (int i = 0; i < count; ++i) {
        auto d = bv.priv().lock();
        WaitUntilFinishedHelper helper(std::bind(&nothing), &d->strand);
        helper.run();
    }
    bv.waitValidationFinished();
}
}

BOOST_FIXTURE_TEST_SUITE(blockvalidation, TestingSetup)

BOOST_AUTO_TEST_CASE(reorderblocks)
{
    bv.appendChain(4);
    BOOST_CHECK_EQUAL(bv.blockchain()->Height(), 4);
    CBlockIndex *oldBlock3 = (*bv.blockchain())[3];
    assert(oldBlock3);
    BOOST_CHECK_EQUAL(oldBlock3->nHeight, 3);
    CBlockIndex *oldBlock4 = (*bv.blockchain())[4];
    assert(oldBlock4);
    BOOST_CHECK_EQUAL(oldBlock4->nHeight, 4);
    BOOST_CHECK(Blocks::DB::instance()->headerChain().Contains(oldBlock3));
    BOOST_CHECK(Blocks::DB::instance()->headerChain().Contains(oldBlock4));

    // Now, build on top of block 3 a 2 block chain. But only register them at the headersChain
    // in the Blocks::DB, so I can test reorgs.
    CKey coinbaseKey;
    coinbaseKey.MakeNewKey();
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    FastBlock b4 = bv.createBlock(oldBlock3, scriptPubKey);
    // printf("B4: %s\n", b4.createHash().ToString().c_str());
    BOOST_CHECK(b4.previousBlockId() == *oldBlock3->phashBlock);
    std::shared_ptr<BlockValidationState> state4(new BlockValidationState(bv.priv(), b4));
    // let it create me a CBlockIndex
    bv.priv().lock()->createBlockIndexFor(state4);
    BOOST_CHECK_EQUAL(state4->m_blockIndex->nHeight, 4);

    // work around optimization of phashblock coming from the hash table.
    uint256 hash4 = state4->m_block.createHash();
    state4->m_blockIndex->phashBlock = &hash4;
    bool changed = Blocks::DB::instance()->appendHeader(state4->m_blockIndex);

    // no reorgs yet.
    BOOST_CHECK_EQUAL(changed, false);
    BOOST_CHECK(Blocks::DB::instance()->headerChain().Contains(oldBlock3));
    BOOST_CHECK(Blocks::DB::instance()->headerChain().Contains(oldBlock4));
    BOOST_CHECK_EQUAL(Blocks::DB::instance()->headerChainTips().size(), 2);

    // The method that does reorgs is the BlocksValidtionPrivate::prepareChainForBlock()
    // We now have two chains as known by the headersChain.
    // the tips have exactly the same POW and as such the new chain should not cause a reorg.
    // (first seen principle)
    bv.priv().lock()->prepareChain();
    BOOST_CHECK_EQUAL(bv.blockchain()->Height(), 4);
    BOOST_CHECK_EQUAL((*bv.blockchain())[3], oldBlock3); // unchanged.
    BOOST_CHECK_EQUAL((*bv.blockchain())[4], oldBlock4);

    FastBlock b5 = bv.createBlock(state4->m_blockIndex, scriptPubKey);
    // printf("B5: %s\n", b5.createHash().ToString().c_str());
    BOOST_CHECK(b5.previousBlockId() == *state4->m_blockIndex->phashBlock);
    std::shared_ptr<BlockValidationState> state5(new BlockValidationState(bv.priv(), b5));
    bv.priv().lock()->createBlockIndexFor(state5);
    BOOST_CHECK_EQUAL(state5->m_blockIndex->pprev, state4->m_blockIndex);
    uint256 hash5 = state5->m_block.createHash();
    state5->m_blockIndex->phashBlock = &hash5;
    changed = Blocks::DB::instance()->appendHeader(state5->m_blockIndex);
    BOOST_CHECK_EQUAL(changed, true);
    BOOST_CHECK_EQUAL(Blocks::DB::instance()->headerChainTips().size(), 2);
    BOOST_CHECK(Blocks::DB::instance()->headerChain().Contains(state4->m_blockIndex));
    BOOST_CHECK(Blocks::DB::instance()->headerChain().Contains(state5->m_blockIndex));

    // We should now get a simple removal of block 4 from the original chain because our
    // new chain has more POW.
    auto d = bv.priv().lock(); // (make sure to call prepareChain in the strand, and avoid an assert)
    WaitUntilFinishedHelper helper(std::bind(&ValidationEnginePrivate::prepareChain, d), &d->strand);
    helper.run();
    BOOST_CHECK_EQUAL(bv.blockchain()->Height(), 3);
    BOOST_CHECK_EQUAL((*bv.blockchain())[3], oldBlock3); // unchanged.
    CBlockIndex *null = nullptr;
    BOOST_CHECK_EQUAL((*bv.blockchain())[4], null);

    bv.shutdown(); // avoid our validation-states being deleted here causing issues.
}

BOOST_AUTO_TEST_CASE(reorderblocks2)
{
    bv.appendChain(20);
    BOOST_CHECK_EQUAL(bv.blockchain()->Height(), 20);

    // create a chain of 8 blocks, forked off after 11.
    CBlockIndex *oldBlock11 = (*bv.blockchain())[11];
    std::vector<FastBlock> blocks = bv.createChain(oldBlock11, 10);
    BOOST_CHECK_EQUAL(blocks.size(), (size_t) 10);
    for (const FastBlock &block : blocks) {
        bv.addBlock(block, Validation::SaveGoodToDisk, nullptr);
    }
    bv.waitValidationFinished();
    BOOST_CHECK_EQUAL(bv.blockchain()->Height(), 21);
    BOOST_CHECK_EQUAL(oldBlock11, (*bv.blockchain())[11]);
    BOOST_CHECK(*(*bv.blockchain())[21]->phashBlock == blocks.back().createHash());
}

BOOST_AUTO_TEST_CASE(detectOrder)
{
    // create a chain of 20 blocks.
    std::vector<FastBlock> blocks = bv.createChain(bv.blockchain()->Tip(), 20);
    // add them all, in reverse order, in order to test if the code is capable of finding the proper ordering of the blocks
    BOOST_REVERSE_FOREACH (const FastBlock &block, blocks) {
        bv.addBlock(block, Validation::SaveGoodToDisk, nullptr);
    }
    bv.waitValidationFinished();
    BOOST_CHECK_EQUAL(bv.blockchain()->Height(), 20);
}

static FastBlock createHeader(const FastBlock &full) {
    return  FastBlock(Streaming::ConstBuffer(full.data().internal_buffer(),
                                             full.data().begin(), full.data().begin() + 80));
}

BOOST_AUTO_TEST_CASE(detectOrder2)
{
    // create a chain of 10 blocks.
    std::vector<FastBlock> blocks = bv.createChain(bv.blockchain()->Tip(), 10);

    // replace one block with a block header.
    FastBlock full = blocks[8];
    FastBlock header = createHeader(full);
    blocks[8] = header;
    for (const FastBlock &block : blocks) {
        bv.addBlock(block, Validation::SaveGoodToDisk, nullptr);
    }
    bv.waitValidationFinished();
    BOOST_CHECK_EQUAL(bv.blockchain()->Height(), 8); // it stopped at the header, not processing the last block because of that.
    bv.addBlock(full, Validation::SaveGoodToDisk, nullptr).start().waitUntilFinished();
    // now we have processed 8, it will continue to process 9 in a different thread.
    waitForStrand(bv);
    BOOST_CHECK_EQUAL(bv.blockchain()->Height(), 10);

    // now again, but with a bigger gap than 1
    blocks = bv.createChain(bv.blockchain()->Tip(), 10);
    std::vector<FastBlock> copy(blocks);
    for (size_t i = 3; i < 7; ++i) {
        blocks[i] = createHeader(blocks[i]);
    }
    for (const FastBlock &block : blocks) {
        bv.addBlock(block, Validation::SaveGoodToDisk, nullptr);
    }
    bv.waitValidationFinished();
    BOOST_CHECK_EQUAL(bv.blockchain()->Height(), 13);

    logDebug() << "again";
    // add them again, in reverse order, in order to test if the code is capable of finding the proper ordering of the blocks
    BOOST_REVERSE_FOREACH (const FastBlock &block, copy) {
        bv.addBlock(block, Validation::SaveGoodToDisk, nullptr);
    }
    bv.waitValidationFinished();
    waitForStrand(bv);
    bv.waitValidationFinished();
    BOOST_CHECK_EQUAL(bv.blockchain()->Height(), 20);
}

BOOST_AUTO_TEST_CASE(duplicateInput)
{
    CKey coinbaseKey;
    // create a chain of 101 blocks.
    std::vector<FastBlock> blocks = bv.appendChain(101, coinbaseKey);
    assert(blocks.size() == 101);
    CMutableTransaction newTx;
    newTx.vout.resize(1);
    newTx.vout[0].nValue = 11 * CENT;
    CScript scriptPubKey = CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    newTx.vout[0].scriptPubKey = scriptPubKey;
    CTxIn input;
    input.prevout.n = 0;
    input.prevout.hash = blocks.front().createHash();
    newTx.vin.push_back(input);
    newTx.vin.push_back(input); // duplicate input

    // Sign
    std::vector<unsigned char> vchSig;
    uint256 hash = SignatureHash(scriptPubKey, newTx, 0, 50 * COIN, SIGHASH_ALL | SIGHASH_FORKID, SCRIPT_ENABLE_SIGHASH_FORKID);
    BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
    vchSig.push_back((unsigned char)SIGHASH_ALL + SIGHASH_FORKID);
    newTx.vin[0].scriptSig << vchSig;
    newTx.vin[1].scriptSig << vchSig;

    FastBlock newBlock = bv.createBlock(bv.blockchain()->Tip());
    {
        CBlock block = newBlock.createOldBlock();
        block.vtx.push_back(newTx);
        newBlock = FastBlock::fromOldBlock(block);
        BOOST_CHECK_EQUAL(block.vtx.size(), 2);
    }
    auto future = bv.addBlock(newBlock, Validation::SaveGoodToDisk);
    future.setCheckPoW(false);
    future.setCheckMerkleRoot(false);
    future.start();
    future.waitUntilFinished();
    BOOST_CHECK_EQUAL(future.error(), std::string("bad-txns-inputs-duplicate"));
}

// this only works if the input is a p2pkh script!
CTransaction splitCoins(const Tx &inTx, int inIndex, const CKey &from, const CKey &to, int outputCount)
{
    assert(outputCount > 0);
    assert(inIndex >= 0);
logInfo() << inTx.createHash();

    Tx::Output prevOut = inTx.output(inIndex);
    assert(prevOut.outputValue > 0);
    const uint64_t outAmount = prevOut.outputValue / outputCount;
    assert(outAmount > 5);

    CMutableTransaction newTx;
    CTxIn input;
    input.prevout.n = inIndex;
    input.prevout.hash = inTx.createHash();
    newTx.vin.push_back(input);

    const CScript scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(to.GetPubKey().GetID())
                                     << OP_EQUALVERIFY << OP_CHECKSIG;
    newTx.vout.resize(outputCount);
    for (int i = 0; i < outputCount; ++i) {
        newTx.vout[i].nValue = outAmount;
        newTx.vout[i].scriptPubKey = scriptPubKey;
    }

    // Sign
    const int nHashType = SIGHASH_ALL | SIGHASH_FORKID;
    const uint256 sigHash = SignatureHash(prevOut.outputScript, newTx, inIndex, prevOut.outputValue, nHashType,  SCRIPT_ENABLE_SIGHASH_FORKID);
    std::vector<unsigned char> vchSig;
    bool ok = from.Sign(sigHash, vchSig);
    assert(ok);
    vchSig.push_back((unsigned char)nHashType);
    newTx.vin[0].scriptSig << vchSig;
    newTx.vin[0].scriptSig << ToByteVector(from.GetPubKey());

    return newTx;
}

BOOST_AUTO_TEST_CASE(CTOR)
{
    auto priv = bv.priv().lock();
    priv->tipFlags.hf201811Active = true;

    CKey myKey;
    // create a chain of 101 blocks.
    std::vector<FastBlock> blocks = bv.appendChain(110, myKey, MockBlockValidation::FullOutScript);
    assert(blocks.size() == 110);

    FastBlock block1 = blocks.at(1);
    block1.findTransactions();
    const int OUTPUT_COUNT = 100;
    std::vector<CTransaction> txs;
    CTransaction root = splitCoins(block1.transactions().at(0),
                                   0, myKey, myKey, OUTPUT_COUNT);
    txs.push_back(root);
    for (int i = 1; i < 5; ++i) {
        txs.push_back(splitCoins(Tx::fromOldTransaction(root), i, myKey, myKey, 10));
    }
    for (size_t i = 0; i < txs.size(); ++i) {
        // logDebug() << "tx" << i << txs.at(i).GetHash() << "in" << txs.at(i).vin.size()
        //  << "out" << txs.at(i).vout.size();
    }

    CKey coinbaseKey;
    coinbaseKey.MakeNewKey();
    CScript scriptPubKey;
    scriptPubKey << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    FastBlock unsortedBlock = bv.createBlock(bv.blockchain()->Tip(),  scriptPubKey, txs);

    auto future = bv.addBlock(unsortedBlock, Validation::SaveGoodToDisk).start();
    future.waitUntilFinished();
    BOOST_CHECK_EQUAL("tx-ordering-not-CTOR", future.error());

    // sort the transactions and then mine it again.
    std::sort(txs.begin(), txs.end(), &CTransaction::sortTxByTxId);
    FastBlock sortedBlock = bv.createBlock(bv.blockchain()->Tip(),  scriptPubKey, txs);
    future = bv.addBlock(sortedBlock, Validation::SaveGoodToDisk).start();
    future.waitUntilFinished();
    // I intended the actual validation to go fully Ok, but I get some signature failures.
    BOOST_CHECK("tx-ordering-not-CTOR" != future.error());
    BOOST_CHECK("missing-inputs" != future.error());
}


BOOST_AUTO_TEST_SUITE_END()
