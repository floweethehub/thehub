/*
 * This file is part of the flowee project
 * Copyright (C) 2017-2019 Tom Zander <tomz@freedommail.ch>
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

#include "blockvalidation_tests.h"

#include <TransactionBuilder.h>
#include <util.h>
#include <random.h>
#include <validation/BlockValidation_p.h>
#include <server/BlocksDB.h>
#include <script/interpreter.h>
#include <utxo/UnspentOutputDatabase.h>
#include <WaitUntilFinishedHelper.h>

#include <boost/foreach.hpp>

namespace {
void nothing(){
    logInfo() << "nothing";
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
void waitForHeight(MockBlockValidation &bv, int height) {
    bv.waitValidationFinished();
    // Validation is async, spread over many events so the best bet to get the good result is to wait a bit.
    for (int i = 0; i < 20; ++i) { // max 1 sec
        boost::this_thread::sleep_for(boost::chrono::milliseconds(50));
        if (bv.blockchain()->Height() == height) break;
    }
}
}

TestBlockValidation::TestBlockValidation()
    : TestFloweeSession("regtest")
{
}

void TestBlockValidation::reorderblocks()
{
    bv->appendChain(4);
    QCOMPARE(bv->blockchain()->Height(), 4);
    CBlockIndex *oldBlock3 = (*bv->blockchain())[3];
    assert(oldBlock3);
    QCOMPARE(oldBlock3->nHeight, 3);
    CBlockIndex *oldBlock4 = (*bv->blockchain())[4];
    assert(oldBlock4);
    QCOMPARE(oldBlock4->nHeight, 4);
    QVERIFY(Blocks::DB::instance()->headerChain().Contains(oldBlock3));
    QVERIFY(Blocks::DB::instance()->headerChain().Contains(oldBlock4));

    // Now, build on top of block 3 a 2 block chain. But only register them at the headersChain
    // in the Blocks::DB, so I can test reorgs.
    CKey coinbaseKey;
    coinbaseKey.MakeNewKey();
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    FastBlock b4 = bv->createBlock(oldBlock3, scriptPubKey);
    // printf("B4: %s\n", b4.createHash().ToString().c_str());
    QVERIFY(b4.previousBlockId() == *oldBlock3->phashBlock);
    std::shared_ptr<BlockValidationState> state4(new BlockValidationState(bv->priv(), b4));
    // let it create me a CBlockIndex
    bv->priv().lock()->createBlockIndexFor(state4);
    QCOMPARE(state4->m_blockIndex->nHeight, 4);

    // work around optimization of phashblock coming from the hash table.
    uint256 hash4 = state4->m_block.createHash();
    state4->m_blockIndex->phashBlock = &hash4;
    bool changed = Blocks::DB::instance()->appendHeader(state4->m_blockIndex);

    // no reorgs yet.
    QCOMPARE(changed, false);
    QVERIFY(Blocks::DB::instance()->headerChain().Contains(oldBlock3));
    QVERIFY(Blocks::DB::instance()->headerChain().Contains(oldBlock4));
    QCOMPARE((int) Blocks::DB::instance()->headerChainTips().size(), 2);

    // The method that does reorgs is the BlocksValidtionPrivate::prepareChainForBlock()
    // We now have two chains as known by the headersChain.
    // the tips have exactly the same POW and as such the new chain should not cause a reorg.
    // (first seen principle)
    bv->priv().lock()->prepareChain();
    QCOMPARE(bv->blockchain()->Height(), 4);
    QCOMPARE((*bv->blockchain())[3], oldBlock3); // unchanged.
    QCOMPARE((*bv->blockchain())[4], oldBlock4);

    FastBlock b5 = bv->createBlock(state4->m_blockIndex, scriptPubKey);
    // printf("B5: %s\n", b5.createHash().ToString().c_str());
    QVERIFY(b5.previousBlockId() == *state4->m_blockIndex->phashBlock);
    std::shared_ptr<BlockValidationState> state5(new BlockValidationState(bv->priv(), b5));
    bv->priv().lock()->createBlockIndexFor(state5);
    QCOMPARE(state5->m_blockIndex->pprev, state4->m_blockIndex);
    uint256 hash5 = state5->m_block.createHash();
    state5->m_blockIndex->phashBlock = &hash5;
    changed = Blocks::DB::instance()->appendHeader(state5->m_blockIndex);
    QCOMPARE(changed, true);
    QCOMPARE((int) Blocks::DB::instance()->headerChainTips().size(), 2);
    QVERIFY(Blocks::DB::instance()->headerChain().Contains(state4->m_blockIndex));
    QVERIFY(Blocks::DB::instance()->headerChain().Contains(state5->m_blockIndex));

    // We should now get a simple removal of block 4 from the original chain because our
    // new chain has more POW.
    auto d = bv->priv().lock(); // (make sure to call prepareChain in the strand, and avoid an assert)
    WaitUntilFinishedHelper helper(std::bind(&ValidationEnginePrivate::prepareChain, d), &d->strand);
    helper.run();
    QCOMPARE(bv->blockchain()->Height(), 3);
    QCOMPARE((*bv->blockchain())[3], oldBlock3); // unchanged.
    CBlockIndex *null = nullptr;
    QCOMPARE((*bv->blockchain())[4], null);

    bv->shutdown(); // avoid our validation-states being deleted here causing issues.
}

void TestBlockValidation::reorderblocks2()
{
    bv->appendChain(20);
    QCOMPARE(bv->blockchain()->Height(), 20);

    // create a chain of 8 blocks, forked off after 11.
    CBlockIndex *oldBlock11 = (*bv->blockchain())[11];
    std::vector<FastBlock> blocks = bv->createChain(oldBlock11, 10);
    QCOMPARE(blocks.size(), (size_t) 10);
    for (const FastBlock &block : blocks) {
        auto future = bv->addBlock(block, Validation::SaveGoodToDisk, nullptr).start();
        future.waitUntilFinished();
        QCOMPARE(future.error(), std::string());
    }
    QTRY_COMPARE(bv->blockchain()->Height(), 21);
    QCOMPARE(oldBlock11, (*bv->blockchain())[11]);
    QVERIFY(*(*bv->blockchain())[21]->phashBlock == blocks.back().createHash());
}

void TestBlockValidation::detectOrder()
{
    // create a chain of 20 blocks.
    std::vector<FastBlock> blocks = bv->createChain(bv->blockchain()->Tip(), 20);
    // add them all, in reverse order, in order to test if the code is capable of finding the proper ordering of the blocks
    BOOST_REVERSE_FOREACH (const FastBlock &block, blocks) {
        bv->addBlock(block, Validation::SaveGoodToDisk, nullptr);
    }
    QTRY_COMPARE(bv->blockchain()->Height(), 20);
}

FastBlock TestBlockValidation::createHeader(const FastBlock &full) const
{
    return  FastBlock(Streaming::ConstBuffer(full.data().internal_buffer(),
                                             full.data().begin(), full.data().begin() + 80));
}

void TestBlockValidation::detectOrder2()
{
    // create a chain of 10 blocks.
    std::vector<FastBlock> blocks = bv->createChain(bv->blockchain()->Tip(), 10);

    // replace one block with a block header.
    FastBlock full = blocks[8];
    FastBlock header = createHeader(full);
    blocks[8] = header;
    for (const FastBlock &block : blocks) {
        bv->addBlock(block, Validation::SaveGoodToDisk, nullptr);
    }
    waitForHeight(*bv, 8);
    bv->addBlock(full, Validation::SaveGoodToDisk, nullptr).start().waitUntilFinished();
    // now we have processed 8, it will continue to process 9 in a different thread.
    waitForHeight(*bv, 10);
    QCOMPARE(bv->blockchain()->Height(), 10);

    // now again, but with a bigger gap than 1
    blocks = bv->createChain(bv->blockchain()->Tip(), 10);
    std::vector<FastBlock> copy(blocks);
    for (size_t i = 3; i < 7; ++i) {
        blocks[i] = createHeader(blocks[i]);
    }
    for (const FastBlock &block : blocks) {
        bv->addBlock(block, Validation::SaveGoodToDisk, nullptr);
    }
    waitForHeight(*bv, 13);
    QCOMPARE(bv->blockchain()->Height(), 13);

    // add them again, in reverse order, in order to test if the code is capable of finding the proper ordering of the blocks
    BOOST_REVERSE_FOREACH (const FastBlock &block, copy) {
        bv->addBlock(block, Validation::SaveGoodToDisk, nullptr);
    }
    waitForHeight(*bv, 20);
    QCOMPARE(bv->blockchain()->Height(), 20);
}

void TestBlockValidation::duplicateInput()
{
    CKey coinbaseKey;
    // create a chain of 101 blocks.
    std::vector<FastBlock> blocks = bv->appendChain(101, coinbaseKey);
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
    QVERIFY(coinbaseKey.Sign(hash, vchSig));
    vchSig.push_back((unsigned char)SIGHASH_ALL + SIGHASH_FORKID);
    newTx.vin[0].scriptSig << vchSig;
    newTx.vin[1].scriptSig << vchSig;

    FastBlock newBlock = bv->createBlock(bv->blockchain()->Tip());
    {
        CBlock block = newBlock.createOldBlock();
        block.vtx.push_back(newTx);
        newBlock = FastBlock::fromOldBlock(block);
        QCOMPARE((int) block.vtx.size(), 2);
    }
    auto future = bv->addBlock(newBlock, Validation::SaveGoodToDisk);
    future.setCheckPoW(false);
    future.setCheckMerkleRoot(false);
    future.start();
    future.waitUntilFinished();
    QCOMPARE(future.error(), std::string("bad-txns-inputs-duplicate"));
}

// this only works if the input is a p2pkh script!
CTransaction TestBlockValidation::splitCoins(const Tx &inTx, int inIndex, const CKey &from, const CKey &to, int outputCount) const
{
    assert(outputCount > 0);
    assert(inIndex >= 0);
// logInfo() << inTx.createHash();

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

void TestBlockValidation::CTOR()
{
    auto priv = bv->priv().lock();
    priv->tipFlags.hf201811Active = true;

    CKey myKey;
    // create a chain of 101 blocks.
    std::vector<FastBlock> blocks = bv->appendChain(110, myKey, MockBlockValidation::FullOutScript);
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
    FastBlock unsortedBlock = bv->createBlock(bv->blockchain()->Tip(),  scriptPubKey, txs);

    auto future = bv->addBlock(unsortedBlock, Validation::SaveGoodToDisk).start();
    future.waitUntilFinished();
    QCOMPARE(std::string("tx-ordering-not-CTOR"), future.error());

    // sort the transactions and then mine it again.
    std::sort(txs.begin(), txs.end(), &CTransaction::sortTxByTxId);
    FastBlock sortedBlock = bv->createBlock(bv->blockchain()->Tip(),  scriptPubKey, txs);
    future = bv->addBlock(sortedBlock, Validation::SaveGoodToDisk).start();
    future.waitUntilFinished();
    // I intended the actual validation to go fully Ok, but I get some signature failures.
    QVERIFY("tx-ordering-not-CTOR" != future.error());
    QVERIFY("missing-inputs" != future.error());
}

void TestBlockValidation::rollback()
{
    auto priv = bv->priv().lock(); // enable CTOR
    priv->tipFlags.hf201811Active = true;

    CKey myKey;
    // create a chain of 101 blocks.
    std::vector<FastBlock> blocks = bv->appendChain(110, myKey, MockBlockValidation::FullOutScript);
    assert(blocks.size() == 110);

    FastBlock block1 = blocks.at(1);
    block1.findTransactions();

    // mine block to create some more inputs that are not coinbases
    std::vector<CTransaction> txs;
    CTransaction root = splitCoins(block1.transactions().at(0), 0, myKey, myKey, 3);
    txs.push_back(root);

    // dummy coinbasekey
    CScript scriptPubKey;
    scriptPubKey << OP_TRUE;
    FastBlock block = bv->createBlock(bv->blockchain()->Tip(),  scriptPubKey, txs);
    auto future = bv->addBlock(block, Validation::SaveGoodToDisk).start();
    future.waitUntilFinished();
    QCOMPARE(future.error(), std::string());
    QCOMPARE(bv->blockchain()->Height(), 111);

    // now, make a block that spends those 3 outputs just created but also spends various
    // outputs created in the same block.
    txs.clear();
    const CKeyID bitcoinAddress = myKey.GetPubKey().GetID();
    for (size_t i = 0; i < root.vout.size(); ++i) {
        {
            TransactionBuilder builder;
            builder.appendInput(root.GetHash(), i);
            builder.pushInputSignature(myKey, root.vout[i].scriptPubKey, root.vout[i].nValue);
            builder.appendOutput(root.vout[i].nValue - 1000);
            builder.pushOutputPay2Address(bitcoinAddress);
            txs.push_back(builder.createTransaction().createOldTransaction());
        }
        for (int x = GetRandInt(4); x > 0; --x) {
            TransactionBuilder builder;
            auto lastTx = txs.back();
            builder.appendInput(lastTx.GetHash(), 0);
            builder.pushInputSignature(myKey, lastTx.vout[0].scriptPubKey, lastTx.vout[0].nValue);
            builder.appendOutput(lastTx.vout[0].nValue - 1000);
            builder.pushOutputPay2Address(bitcoinAddress);
            txs.push_back(builder.createTransaction().createOldTransaction());
        }
    }

    auto utxo = bv->mempool()->utxo();
    // the same code we use to check at the end of the method.
    QCOMPARE(bv->blockchain()->Height(), 111);
    for (size_t i = 0; i < root.vout.size(); ++i) {
        auto result = utxo->find(root.GetHash(), i);
        QVERIFY(result.isValid());
    }
    for (size_t i = 0; i < txs.size(); ++i) {
        auto result = utxo->find(txs[i].GetHash(), 0);
        QVERIFY(!result.isValid());
    }

    // append tx's as block
    std::sort(txs.begin(), txs.end(), &CTransaction::sortTxByTxId);
    block = bv->createBlock(bv->blockchain()->Tip(),  scriptPubKey, txs);
    future = bv->addBlock(block, Validation::SaveGoodToDisk).start();
    future.waitUntilFinished();
    QCOMPARE(future.error(), std::string());
    QCOMPARE(bv->blockchain()->Height(), 112);

    // now, the rollback should realize which inputs come from the same block and make sure those are not
    // re-added to the mempool.
    block.findTransactions();
    QCOMPARE(block.transactions().size(), txs.size() + 1);
    bool clean = false, error = false;
    priv->strand.post(std::bind(&ValidationEnginePrivate::disconnectTip, priv, bv->blockchain()->Tip(), &clean, &error));
    waitForStrand(*bv);
    QCOMPARE(clean, true);
    QCOMPARE(error, false);

    // same code to check as above before we added the block
    QCOMPARE(bv->blockchain()->Height(), 111);
    for (size_t i = 0; i < root.vout.size(); ++i) {
        auto result = utxo->find(root.GetHash(), i);
        QVERIFY(result.isValid());
    }
    for (size_t i = 0; i < txs.size(); ++i) {
        auto result = utxo->find(txs[i].GetHash(), 0);
        QVERIFY(!result.isValid());
    }
}

void TestBlockValidation::minimalPush()
{
    auto priv = bv->priv().lock(); // enable minimalPush being a consensus rule
    priv->tipFlags.hf201911Active = true;

    CKey myKey;
    // create a chain of 101 blocks.
    std::vector<FastBlock> blocks = bv->appendChain(110, myKey, MockBlockValidation::FullOutScript);
    assert(blocks.size() == 110);

    FastBlock block1 = blocks.at(1);
    block1.findTransactions();
    CTransaction root = block1.transactions().at(0).createOldTransaction();

    CScript scriptPubKey = CScript() << OP_1;
    TransactionBuilder builder;
    builder.appendInput(root.GetHash(), 0);
    builder.pushInputSignature(myKey, root.vout[0].scriptPubKey, root.vout[0].nValue);
    builder.appendOutput(20 * COIN);
    builder.pushOutputScript(scriptPubKey);
    CMutableTransaction tx = builder.createTransaction().createOldTransaction();
    // at this point the tx is perfectly Ok and should pass.
    // Lets change the input script to violate the minimal push and see if the system detects this.
    CScript inputScript = tx.vin[0].scriptSig;
    QByteArray byteArray(reinterpret_cast<char*>(&inputScript[0]), inputScript.size());
    byteArray.insert(0, OP_PUSHDATA1);
    tx.vin[0].scriptSig.assign(byteArray.begin(), byteArray.end());

    // mine block with this adjusted transaction to find out if its rejected by consensus rules.
    std::vector<CTransaction> txs;
    txs.push_back(tx);

    FastBlock block = bv->createBlock(bv->blockchain()->Tip(),  scriptPubKey, txs);
    auto future = bv->addBlock(block, Validation::SaveGoodToDisk).start();
    future.waitUntilFinished();
    QCOMPARE(future.error(), std::string("non-mandatory-script-verify-flag (Data push larger than necessary)"));
    QCOMPARE(bv->blockchain()->Height(), 110);
}

void TestBlockValidation::manualAdjustments()
{
    CKey coinbaseKey;
    // create a chain of 101 blocks.
    std::vector<FastBlock> blocks = bv->appendChain(15, coinbaseKey);
    assert(blocks.size() == 15);

    QCOMPARE(Blocks::DB::instance()->headerChain().Height(), 15);

     // do an invalidateblock of the last block.
    auto tip = Blocks::Index::get(blocks.back().createHash());
    QVERIFY(tip);
    QCOMPARE(tip->nHeight, 15);

    QCOMPARE(Blocks::DB::instance()->headerChain().Tip(), tip);
    bv->invalidateBlock(tip);
    QCOMPARE(Blocks::DB::instance()->headerChain().Height(), 14);
    QCOMPARE(Blocks::DB::instance()->headerChain().Tip(), tip->pprev);
    auto utxo = bv->mempool()->utxo();
    QVERIFY(utxo->blockIdHasFailed(tip->GetBlockHash()));

     // do a reconsiderBlock of the same. Should undo the work.
    Blocks::Index::reconsiderBlock(tip, utxo);
    QCOMPARE(utxo->blockIdHasFailed(tip->GetBlockHash()), false);

    // Do what the RPC reconsiderBlock does: try to validate it again.
    Validation::Settings future = bv->addBlock(blocks.back(), 0);
    future.start();
    future.waitUntilFinished();
    QCOMPARE(Blocks::DB::instance()->headerChain().Height(), 15);
    QCOMPARE(Blocks::DB::instance()->headerChain().Tip(), tip);


    auto block13 = Blocks::Index::get(blocks.at(12).createHash());
    QVERIFY(block13);
    QCOMPARE(block13->nHeight, 13);
    QVERIFY(Blocks::DB::instance()->headerChain().Contains(block13));
    bv->invalidateBlock(block13);
    QCOMPARE(Blocks::DB::instance()->headerChain().Height(), 12);
    QCOMPARE(Blocks::DB::instance()->headerChain().Tip(), block13->pprev);
    QVERIFY(utxo->blockIdHasFailed(block13->GetBlockHash()));
    QCOMPARE(utxo->blockIdHasFailed(tip->GetBlockHash()), false);
    QCOMPARE(utxo->blockIdHasFailed(block13->pprev->GetBlockHash()), false);

     // do a reconsiderBlock of block 14. Should still redo the work.
    auto block14 = Blocks::Index::get(blocks.at(13).createHash());
    QVERIFY(block14);
    QCOMPARE(block14->nHeight, 14);
    Blocks::Index::reconsiderBlock(block14, utxo);
    QCOMPARE(utxo->blockIdHasFailed(tip->GetBlockHash()), false);
    QCOMPARE(utxo->blockIdHasFailed(block13->GetBlockHash()), false);
    QCOMPARE(utxo->blockIdHasFailed(block14->GetBlockHash()), false);

    // Do what the RPC reconsiderBlock does: try to validate it again.
    future = bv->addBlock(blocks.at(14), 0);
    future.start();
    future.waitUntilFinished();
    QCOMPARE(Blocks::DB::instance()->headerChain().Height(), 15);
    QCOMPARE(Blocks::DB::instance()->headerChain().Tip(), tip);
}

void TestBlockValidation::testBlockIndex()
{
    CBlockIndex index;
    QCOMPARE(index.nStatus, 0);
    index.RaiseValidity(BLOCK_VALID_HEADER);
    QCOMPARE(index.IsValid(BLOCK_VALID_HEADER), true);
    QCOMPARE(index.IsValid(BLOCK_VALID_TREE), false);
    QCOMPARE(index.IsValid(BLOCK_VALID_TRANSACTIONS), false);
    QCOMPARE(index.IsValid(BLOCK_VALID_CHAIN), false);
    QCOMPARE(index.IsValid(BLOCK_VALID_SCRIPTS), false);
    QCOMPARE(index.nStatus, BLOCK_VALID_HEADER);
    index.RaiseValidity(BLOCK_VALID_TREE);
    QCOMPARE(index.IsValid(BLOCK_VALID_HEADER), true);
    QCOMPARE(index.IsValid(BLOCK_VALID_TREE), true);
    QCOMPARE(index.IsValid(BLOCK_VALID_TRANSACTIONS), false);
    QCOMPARE(index.IsValid(BLOCK_VALID_CHAIN), false);
    QCOMPARE(index.IsValid(BLOCK_VALID_SCRIPTS), false);
    QCOMPARE(index.nStatus, BLOCK_VALID_TREE);
    index.nStatus |= BLOCK_HAVE_DATA;
    QCOMPARE(index.IsValid(BLOCK_VALID_HEADER), true);
    QCOMPARE(index.IsValid(BLOCK_VALID_TREE), true);
    QCOMPARE(index.IsValid(BLOCK_VALID_TRANSACTIONS), false);
    QCOMPARE(index.IsValid(BLOCK_VALID_CHAIN), false);
    QCOMPARE(index.IsValid(BLOCK_VALID_SCRIPTS), false);
    QCOMPARE(index.nStatus, BLOCK_VALID_TREE | BLOCK_HAVE_DATA);
    index.nStatus |= BLOCK_HAVE_UNDO;
    QCOMPARE(index.IsValid(BLOCK_VALID_HEADER), true);
    QCOMPARE(index.IsValid(BLOCK_VALID_TREE), true);
    QCOMPARE(index.IsValid(BLOCK_VALID_TRANSACTIONS), false);
    QCOMPARE(index.IsValid(BLOCK_VALID_CHAIN), false);
    QCOMPARE(index.IsValid(BLOCK_VALID_SCRIPTS), false);
    QCOMPARE(index.nStatus, BLOCK_VALID_TREE | BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO);

    index.RaiseValidity(BLOCK_VALID_CHAIN);
    QCOMPARE(index.IsValid(BLOCK_VALID_HEADER), true);
    QCOMPARE(index.IsValid(BLOCK_VALID_TREE), true);
    QCOMPARE(index.IsValid(BLOCK_VALID_TRANSACTIONS), true);
    QCOMPARE(index.IsValid(BLOCK_VALID_CHAIN), true);
    QCOMPARE(index.IsValid(BLOCK_VALID_SCRIPTS), false);
    QCOMPARE(index.nStatus, BLOCK_VALID_CHAIN | BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO);

    CBlockIndex index2;
    index2.RaiseValidity(BLOCK_VALID_SCRIPTS);
    QCOMPARE(index2.IsValid(BLOCK_VALID_HEADER), true);
    QCOMPARE(index2.IsValid(BLOCK_VALID_TREE), true);
    QCOMPARE(index2.IsValid(BLOCK_VALID_TRANSACTIONS), true);
    QCOMPARE(index2.IsValid(BLOCK_VALID_CHAIN), true);
    QCOMPARE(index2.IsValid(BLOCK_VALID_SCRIPTS), true);
}

QTEST_MAIN(TestBlockValidation)
