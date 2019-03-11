/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2015 The Bitcoin Core developers
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

#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "main.h"
#include "miner.h"
#include "primitives/pubkey.h"
#include "script/standard.h"
#include "txmempool.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"
#include <primitives/FastBlock.h>
#include <utxo/UnspentOutputDatabase.h>
#include "BlocksDB_p.h" // to access the blockMap directly and use erase

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <boost/foreach.hpp>

BOOST_FIXTURE_TEST_SUITE(miner_tests, MainnetTestingSetup)

CBlockIndex CreateBlockIndex(CBlockIndex *parent, int offset)
{
    CBlockIndex index;
    index.nHeight = parent->nHeight + offset;
    index.pprev = parent;
    return index;
}

bool TestSequenceLocks(CTxMemPool &mempool, const CTransaction &tx, int flags)
{
    LOCK(mempool.cs);
    return CheckSequenceLocks(mempool, tx, flags);
}

// NOTE: These tests rely on CreateNewBlock doing its own self-validation!
BOOST_AUTO_TEST_CASE(CreateNewBlock_validity)
{
    CScript scriptPubKey = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    CBlockTemplate *pblocktemplate;
    CMutableTransaction tx;
    CScript script;
    uint256 hash;
    TestMemPoolEntryHelper entry;
    entry.nFee = 11;
    entry.dPriority = 111.0;
    entry.nHeight = 11;

    fCheckpointsEnabled = false;

    BOOST_CHECK_EQUAL(bv.blockchain()->Height(), 0);
    // Simple block creation, nothing special yet:
    Mining miner;
    miner.SetCoinbase(scriptPubKey);
    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));
    BOOST_CHECK_EQUAL(bv.blockchain()->Height(), 0);
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 1);
    delete pblocktemplate;

    // We can't make transactions until we have inputs
    // Therefore, load 100 blocks :)

    int baseheight = bv.blockchain()->Height();
    auto chain = bv.appendChain(110, MockBlockValidation::EmptyOutScript);
    std::vector<CTransaction*>txFirst;
    for (int i = 0; i < 4; ++i) {
        CBlock block = chain[i].createOldBlock();
        txFirst.push_back(new CTransaction(block.vtx[0]));
    }

    BOOST_CHECK_EQUAL(bv.blockchain()->Height(), 110);

    // Just to make sure we can still make simple blocks
    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));
    delete pblocktemplate;

    // block sigops > limit: 1000 CHECKMULTISIG + 1
    tx.vin.resize(1);
    // NOTE: OP_NOP is used to force 20 SigOps for the CHECKMULTISIG
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_0 << OP_0 << OP_NOP << OP_CHECKMULTISIG << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 5000000000LL;
    for (unsigned int i = 0; i < 1001; ++i)
    {
        tx.vout[0].nValue -= 1000000;
        hash = tx.GetHash();
        const bool spendsCoinbase = i == 0; // only first tx spends coinbase
        // If we don't set the # of sig ops in the CTxMemPoolEntry, template creation fails
        bv.mp.addUnchecked(hash, entry.Fee(1000000).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));
    BOOST_CHECK(!bv.mp.exists(hash));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 1);
    delete pblocktemplate;
    bv.mp.clear();

    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vout[0].nValue = 5000000000LL;
    for (unsigned int i = 0; i < 1; ++i) {
        tx.vout[0].nValue -= 1000000;
        hash = tx.GetHash();
        const bool spendsCoinbase = i == 0; // only first tx spends coinbase
        // If we do set the # of sig ops in the CTxMemPoolEntry, template creation passes
        bv.mp.addUnchecked(hash, entry.Fee(1000000).Time(GetTime()).SpendsCoinbase(spendsCoinbase).SigOps(20).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));
    BOOST_CHECK(pblocktemplate->block.vtx.size() > 1);
    delete pblocktemplate;
    bv.mp.clear();

    // block size > limit
    tx.vin[0].scriptSig = CScript();
    // 18 * (520char + DROP) + OP_1 = 9433 bytes
    std::vector<unsigned char> vchData(520);
    for (unsigned int i = 0; i < 18; ++i)
        tx.vin[0].scriptSig << vchData << OP_DROP;
    tx.vin[0].scriptSig << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vout[0].nValue = 5000000000LL;
    for (unsigned int i = 0; i < 128; ++i)
    {
        tx.vout[0].nValue -= 10000000;
        hash = tx.GetHash();
        bool spendsCoinbase = (i == 0) ? true : false; // only first tx spends coinbase
        bv.mp.addUnchecked(hash, entry.Fee(1000000).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));
    delete pblocktemplate;
    bv.mp.clear();

    // orphan in mempool not mined
    hash = tx.GetHash();
    bv.mp.addUnchecked(hash, entry.Fee(1000000).Time(GetTime()).FromTx(tx));
    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 1);
    delete pblocktemplate;
    bv.mp.clear();

    // child with higher priority than parent
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vout[0].nValue = 4900000000LL;
    hash = tx.GetHash();
    bv.mp.addUnchecked(hash, entry.Fee(100000000LL).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin.resize(2);
    tx.vin[1].scriptSig = CScript() << OP_1;
    tx.vin[1].prevout.hash = txFirst[0]->GetHash();
    tx.vin[1].prevout.n = 0;
    tx.vout[0].nValue = 5900000000LL;
    hash = tx.GetHash();
    bv.mp.addUnchecked(hash, entry.Fee(400000000LL).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));
    delete pblocktemplate;
    bv.mp.clear();

    // coinbase in mempool, template creation fails
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_1;
    tx.vout[0].nValue = 0;
    hash = tx.GetHash();
    // give it a fee so it'll get mined
    bv.mp.addUnchecked(hash, entry.Fee(100000).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 1);
    delete pblocktemplate;
    bv.mp.clear();

    // invalid (pre-p2sh) txn in mempool, don't mine
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = 4900000000LL;
    script = CScript() << OP_0;
    tx.vout[0].scriptPubKey = GetScriptForDestination(CScriptID(script));
    hash = tx.GetHash();
    bv.mp.addUnchecked(hash, entry.Fee(10000000L).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(script.begin(), script.end());
    tx.vout[0].nValue -= 1000000;
    hash = tx.GetHash();
    bv.mp.addUnchecked(hash, entry.Fee(1000000).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 1); // Just coinbase
    delete pblocktemplate;
    bv.mp.clear();

    // double spend txn pair in mempool, don't mine
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = 4900000000LL;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    hash = tx.GetHash();
    bv.mp.addUnchecked(hash, entry.Fee(100000000L).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vout[0].scriptPubKey = CScript() << OP_2;
    hash = tx.GetHash();
    bv.mp.addUnchecked(hash, entry.Fee(100000000L).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 1); // Just coinbase
    delete pblocktemplate;
    bv.mp.clear();

    // subsidy changing
    int nHeight = bv.blockchain()->Height();
    // Create an actual 209999-long block chain (without valid blocks).
    while (bv.blockchain()->Tip()->nHeight < 209999) {
        CBlockIndex* prev = bv.blockchain()->Tip();
        CBlockIndex* next = new CBlockIndex();

        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->phashBlock = Blocks::Index::insert(GetRandHash(), next);
        next->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
        next->BuildSkip();
        bv.blockchain()->SetTip(next);
        g_utxo->blockFinished(next->nHeight, next->GetBlockHash());
        Blocks::DB::instance()->appendHeader(next);
    }
    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));
    delete pblocktemplate;
    // Extend to a 210000-long block chain.
    while (bv.blockchain()->Tip()->nHeight < 210000) {
        CBlockIndex* prev = bv.blockchain()->Tip();
        CBlockIndex* next = new CBlockIndex();

        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->phashBlock = Blocks::Index::insert(GetRandHash(), next);
        next->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
        next->BuildSkip();
        bv.blockchain()->SetTip(next);
        g_utxo->blockFinished(next->nHeight, next->GetBlockHash());
        Blocks::DB::instance()->appendHeader(next);
    }
    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));
    delete pblocktemplate;
    // Delete the dummy blocks again.
    while (bv.blockchain()->Tip()->nHeight > nHeight) {
        CBlockIndex* del = bv.blockchain()->Tip();
        del->nStatus |= BLOCK_FAILED_VALID;
        Blocks::DB::instance()->appendHeader(del);
        bv.blockchain()->SetTip(del->pprev);
        g_utxo->blockFinished(del->pprev->nHeight, del->pprev->GetBlockHash());
        Blocks::DB::instance()->priv()->indexMap.erase(del->GetBlockHash());
        delete del;
    }

    // non-final txs in mempool
    SetMockTime(bv.blockchain()->Tip()->GetMedianTimePast()+1);
    int flags = LOCKTIME_VERIFY_SEQUENCE|LOCKTIME_MEDIAN_TIME_PAST;
    // height map
    std::vector<int> prevheights;

    // relative height locked
    tx.nVersion = 2;
    tx.vin.resize(1);
    prevheights.resize(1);
    tx.vin[0].prevout.hash = txFirst[0]->GetHash(); // only 1 transaction
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].nSequence = bv.blockchain()->Tip()->nHeight + 1; // txFirst[0] is the 2nd block
    prevheights[0] = baseheight + 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = 4900000000LL;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    tx.nLockTime = 0;
    hash = tx.GetHash();
    bv.mp.addUnchecked(hash, entry.Fee(100000000L).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(bv.mp, tx, flags)); // Sequence locks fail
    BOOST_CHECK(SequenceLocks(tx, flags, &prevheights, CreateBlockIndex(bv.blockchain()->Tip(), 2))); // Sequence locks pass on 2nd block

    // relative time locked
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | (((bv.blockchain()->Tip()->GetMedianTimePast()+1-(*bv.blockchain())[1]->GetMedianTimePast()) >> CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) + 1); // txFirst[1] is the 3rd block
    prevheights[0] = baseheight + 2;
    hash = tx.GetHash();
    bv.mp.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(bv.mp, tx, flags)); // Sequence locks fail

    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        bv.blockchain()->Tip()->GetAncestor(bv.blockchain()->Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast
    BOOST_CHECK(SequenceLocks(tx, flags, &prevheights, CreateBlockIndex(bv.blockchain()->Tip(), 1))); // Sequence locks pass 512 seconds later
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        bv.blockchain()->Tip()->GetAncestor(bv.blockchain()->Tip()->nHeight - i)->nTime -= 512; //undo tricked MTP

    // absolute height locked
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL - 1;
    prevheights[0] = baseheight + 3;
    tx.nLockTime = bv.blockchain()->Tip()->nHeight + 1;
    hash = tx.GetHash();
    bv.mp.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(tx, flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(bv.mp, tx, flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(tx, bv.blockchain()->Tip()->nHeight + 2, bv.blockchain()->Tip()->GetMedianTimePast())); // Locktime passes on 2nd block

    // absolute time locked
    tx.vin[0].prevout.hash = txFirst[3]->GetHash();
    tx.nLockTime = bv.blockchain()->Tip()->GetMedianTimePast();
    prevheights.resize(1);
    prevheights[0] = baseheight + 4;
    hash = tx.GetHash();
    bv.mp.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(tx, flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(bv.mp, tx, flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(tx, bv.blockchain()->Tip()->nHeight + 2, bv.blockchain()->Tip()->GetMedianTimePast() + 1)); // Locktime passes 1 second later

    // mempool-dependent transactions (not added)
    tx.vin[0].prevout.hash = hash;
    prevheights[0] = bv.blockchain()->Tip()->nHeight + 1;
    tx.nLockTime = 0;
    tx.vin[0].nSequence = 0;
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(TestSequenceLocks(bv.mp, tx, flags)); // Sequence locks pass
    tx.vin[0].nSequence = 1;
    BOOST_CHECK(!TestSequenceLocks(bv.mp, tx, flags)); // Sequence locks fail
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
    BOOST_CHECK(TestSequenceLocks(bv.mp, tx, flags)); // Sequence locks pass
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    BOOST_CHECK(!TestSequenceLocks(bv.mp, tx, flags)); // Sequence locks fail

    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));

    // None of the of the absolute height/time locked tx should have made
    // it into the template because we still check IsFinalTx in CreateNewBlock,
    // but relative locked txs will if inconsistently added to mempool.
    // For now these will still generate a valid template until BIP68 soft fork
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 3);
    delete pblocktemplate;
    // However if we advance height by 1 and time by 512, all of them should be mined
    bv.appendChain(1);
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        bv.blockchain()->Tip()->GetAncestor(bv.blockchain()->Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast
    SetMockTime(bv.blockchain()->Tip()->GetMedianTimePast() + 1);

    BOOST_CHECK(pblocktemplate = miner.CreateNewBlock(bv));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 5);
    delete pblocktemplate;

    bv.blockchain()->Tip()->nHeight--;
    SetMockTime(0);
    bv.mp.clear();

    BOOST_FOREACH(CTransaction *tx, txFirst)
        delete tx;

    fCheckpointsEnabled = true;
}

BOOST_AUTO_TEST_SUITE_END()
