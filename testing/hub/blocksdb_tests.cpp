/*
 * This file is part of the Flowee project
 * Copyright (C) 2017-2020 Tom Zander <tom@flowee.org>
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

#include "blocksdb_tests.h"
#include <BlocksDB.h>
#include <primitives/FastBlock.h>
#include <chain.h>

static bool contains(const std::list<CBlockIndex*> &haystack, CBlockIndex *needle)
{
    // and the stl designers wonder why people say C++ is too verbose and hard to use.
    // would it really have hurt us if they had a std::list<T>::contains(T t) method?
    return (std::find(haystack.begin(), haystack.end(), needle) != haystack.end());
}

void TestBlocksDB::headersChain()
{
    uint256 dummyHash;
    CBlockIndex root;
    root.nHeight = 0;
    root.phashBlock = &dummyHash;
    CBlockIndex b1;
    b1.nChainWork = 0x10;
    b1.nHeight = 1;
    b1.pprev = &root;
    b1.phashBlock = &dummyHash;

    CBlockIndex b2;
    b2.pprev = &b1;
    b2.nHeight = 2;
    b2.nChainWork = 0x20;
    b2.phashBlock = &dummyHash;

    CBlockIndex b3;
    b3.pprev = &b2;
    b3.nHeight = 3;
    b3.nChainWork = 0x30;
    b3.phashBlock = &dummyHash;

    CBlockIndex b4;
    b4.pprev = &b3;
    b4.nHeight = 4;
    b4.nChainWork = 0x40;
    b4.phashBlock = &dummyHash;

    CBlockIndex bp3;
    bp3.pprev = &b2;
    bp3.nHeight = 3;
    bp3.nChainWork = 0x31;
    bp3.phashBlock = &dummyHash;

    CBlockIndex bp4;
    bp4.pprev = &bp3;
    bp4.nHeight = 4;
    bp4.nChainWork = 0x41;
    bp4.phashBlock = &dummyHash;

    {
        Blocks::DB::createTestInstance(100);
        Blocks::DB *db = Blocks::DB::instance();
        bool changed = db->appendHeader(&root);

        QCOMPARE(changed, true);
        QCOMPARE(db->headerChain().Tip(), &root);
        QCOMPARE(db->headerChainTips().size(), 1);
        QCOMPARE(db->headerChainTips().front(), &root);

        changed = db->appendHeader(&b1);
        QCOMPARE(changed, true);
        QCOMPARE(db->headerChain().Tip(), &b1);
        QCOMPARE(db->headerChainTips().size(), 1);
        QCOMPARE(db->headerChainTips().front(), &b1);

        changed = db->appendHeader(&b4);
        QCOMPARE(changed, true);
        QCOMPARE(db->headerChain().Tip(), &b4);
        QCOMPARE(db->headerChain().Height(), 4);
        QCOMPARE(db->headerChainTips().size(), 1);
        QCOMPARE(db->headerChainTips().front(), &b4);

        changed = db->appendHeader(&bp3);
        QCOMPARE(changed, false);
        QCOMPARE(db->headerChain().Tip(), &b4);
        QCOMPARE(db->headerChain().Height(), 4);
        QCOMPARE(db->headerChainTips().size(), 2);
        QVERIFY(contains(db->headerChainTips(), &b4));
        QVERIFY(contains(db->headerChainTips(), &bp3));

        changed = db->appendHeader(&bp4);
        QCOMPARE(changed, true);
        QCOMPARE(db->headerChain().Tip(), &bp4);
        QCOMPARE(db->headerChain().Height(), 4);
        QCOMPARE(db->headerChainTips().size(), 2);
        QVERIFY(contains(db->headerChainTips(), &b4));
        QVERIFY(contains(db->headerChainTips(), &bp4));


        QCOMPARE(db->headerChain()[0], &root);
        QCOMPARE(db->headerChain()[1], &b1);
        QCOMPARE(db->headerChain()[2], &b2);
        QCOMPARE(db->headerChain()[3], &bp3);
        QCOMPARE(db->headerChain()[4], &bp4);
    }

    {
        Blocks::DB::createTestInstance(100);
        Blocks::DB *db = Blocks::DB::instance();
        bool changed = db->appendHeader(&bp3);
        QCOMPARE(changed, true);
        QCOMPARE(db->headerChain().Tip(), &bp3);
        QCOMPARE(db->headerChain().Height(), 3);
        QCOMPARE(db->headerChainTips().size(), 1);
        QCOMPARE(db->headerChainTips().front(), &bp3);

        changed = db->appendHeader(&b3);
        QCOMPARE(changed, false);
        QCOMPARE(db->headerChain().Tip(), &bp3);
        QCOMPARE(db->headerChain().Height(), 3);
        QCOMPARE(db->headerChainTips().size(), 2);
        QVERIFY(contains(db->headerChainTips(), &bp3));
        QVERIFY(contains(db->headerChainTips(), &b3));

        QCOMPARE(db->headerChain()[0], &root);
        QCOMPARE(db->headerChain()[1], &b1);
        QCOMPARE(db->headerChain()[2], &b2);
        QCOMPARE(db->headerChain()[3], &bp3);
    }
    {
        Blocks::DB::createTestInstance(100);
        Blocks::DB *db = Blocks::DB::instance();
        bool changed = db->appendHeader(&b3);
        QCOMPARE(changed, true);
        changed = db->appendHeader(&b2);
        QCOMPARE(changed, false);
        QCOMPARE(db->headerChain().Tip(), &b3);
        QCOMPARE(db->headerChain().Height(), 3);
        QCOMPARE(db->headerChainTips().size(), 1);
        QCOMPARE(db->headerChainTips().front(), &b3);
    }

    {
        Blocks::DB::createTestInstance(100);
        Blocks::DB *db = Blocks::DB::instance();
        bool changed = db->appendHeader(&root);
        changed = db->appendHeader(&b1);
        changed = db->appendHeader(&b2);
        changed = db->appendHeader(&b3);
        bp3.nChainWork = b3.nChainWork;
        changed = db->appendHeader(&bp3);
        QCOMPARE(changed, false);
        QCOMPARE(db->headerChain().Tip(), &b3);
        QCOMPARE(db->headerChain().Height(), 3);
        QCOMPARE(db->headerChainTips().size(), 2);
    }
}

void TestBlocksDB::headersChain2()
{
    uint256 dummyHash;
    CBlockIndex root;
    root.nHeight = 0;
    root.phashBlock = &dummyHash;
    CBlockIndex b1;
    b1.nChainWork = 0x10;
    b1.nHeight = 1;
    b1.pprev = &root;
    b1.phashBlock = &dummyHash;

    CBlockIndex b2;
    b2.pprev = &b1;
    b2.nHeight = 2;
    b2.nChainWork = 0x20;
    b2.phashBlock = &dummyHash;

    CBlockIndex b3;
    b3.pprev = &b2;
    b3.nHeight = 3;
    b3.nChainWork = 0x30;
    b3.phashBlock = &dummyHash;

    {
        Blocks::DB::createTestInstance(100);
        Blocks::DB *db = Blocks::DB::instance();
        bool changed = db->appendHeader(&root);
        changed = db->appendHeader(&b1);
        changed = db->appendHeader(&b2);
        changed = db->appendHeader(&b3);

        b3.nStatus |= BLOCK_FAILED_VALID;

        changed = db->appendHeader(&b3);
        QCOMPARE(changed, true);
        QCOMPARE(db->headerChain().Tip(), &b2);
        QCOMPARE(db->headerChain().Height(), 2);
        QCOMPARE(db->headerChainTips().size(), 1);
        QCOMPARE(db->headerChainTips().front(), &b2);
    }

    b3.nStatus = 0;

    {
        Blocks::DB::createTestInstance(100);
        Blocks::DB *db = Blocks::DB::instance();
        bool changed = db->appendHeader(&root);
        changed = db->appendHeader(&b1);
        changed = db->appendHeader(&b2);
        changed = db->appendHeader(&b3);

        b2.nStatus |= BLOCK_FAILED_VALID;

        changed = db->appendHeader(&b2);
        QCOMPARE(changed, true);
        QCOMPARE(db->headerChain().Tip(), &b1);
        QCOMPARE(db->headerChain().Height(), 1);
        QCOMPARE(db->headerChainTips().size(), 1);
        QCOMPARE(db->headerChainTips().front(), &b1);
    }
}

void TestBlocksDB::invalidate()
{
    // create a chain of 20 blocks.
    std::vector<FastBlock> blocks = bv->appendChain(20);
    // split the chain so we have two header-chain-tips
    CBlockIndex *b18 = Blocks::Index::get(blocks.at(18).createHash());
    auto block = bv->createBlock(b18);
    bv->addBlock(block, 0).start().waitUntilFinished();
    QCOMPARE(Blocks::DB::instance()->headerChainTips().size(), 2);

    // then invalidate a block in the common history of both chains
    CBlockIndex *b14 = Blocks::Index::get(blocks.at(14).createHash());
    QVERIFY(b14);
    b14->nStatus |= BLOCK_FAILED_VALID;
    bool changed = Blocks::DB::instance()->appendHeader(b14);
    QVERIFY(changed);
    QCOMPARE(Blocks::DB::instance()->headerChain().Tip(), b14->pprev);

    for (auto tip : Blocks::DB::instance()->headerChainTips()) {
        QCOMPARE(tip, b14->pprev);
    }
    QCOMPARE(Blocks::DB::instance()->headerChainTips().size(), 1);
}

void TestBlocksDB::invalidate2()
{
    /*
     * x b8 b9
     *   \
     *    b9b b10b
     *
     * Invalidating 'b9b' should remove the second branch with b10
     */

    std::vector<FastBlock> blocks = bv->appendChain(10);
    // split the chain so we have two header-chain-tips
    CBlockIndex *b9 = Blocks::Index::get(blocks.at(9).createHash()); // chain-tip
    QCOMPARE(Blocks::DB::instance()->headerChain().Tip(), b9);

    CBlockIndex *b8 = Blocks::Index::get(blocks.at(8).createHash());
    auto block = bv->createBlock(b8);
    bv->addBlock(block, 0).start().waitUntilFinished();
    QCOMPARE(Blocks::DB::instance()->headerChainTips().size(), 2);

    CBlockIndex *b9b = Blocks::Index::get(block.createHash());
    block = bv->createBlock(b9b); // new chain-tip
    bv->addBlock(block, 0).start().waitUntilFinished();
    QCOMPARE(Blocks::DB::instance()->headerChainTips().size(), 2);

    CBlockIndex *b10b = Blocks::Index::get(block.createHash());
    QCOMPARE(Blocks::DB::instance()->headerChain().Tip(), b10b);

    // then invalidate block b9b
    b9b->nStatus |= BLOCK_FAILED_VALID;
    bool changed = Blocks::DB::instance()->appendHeader(b9b);
    QVERIFY(changed);
    QCOMPARE(Blocks::DB::instance()->headerChain().Tip(), b9);
    QCOMPARE(Blocks::DB::instance()->headerChainTips().size(), 1);
}

void TestBlocksDB::invalidate3()
{
    /*
     * b6 b7 b8  b9
     *  \
     *   b7` b8` b9` b10`
     *
     * Create competing chain until reorg.
     * Then invalidate b8` and check if we go back to b9
     */

    std::vector<FastBlock> blocks = bv->appendChain(10);
    // split the chain so we have two header-chain-tips
    const CBlockIndex *b9 = Blocks::Index::get(blocks.at(9).createHash()); // chain-tip
    QCOMPARE(Blocks::DB::instance()->headerChain().Tip(), b9);

    CBlockIndex *b6 = Blocks::Index::get(blocks.at(6).createHash());
    CBlockIndex *b8b = nullptr;
    CBlockIndex *parent = b6;
    for (int i = 0; i < 4; ++i) {
        auto block = bv->createBlock(parent);
        bv->addBlock(block, 0).start().waitUntilFinished();
        parent = Blocks::Index::get(block.createHash());
        if (parent->nHeight == 9)
            b8b = parent;
        QCOMPARE(Blocks::DB::instance()->headerChainTips().size(), 2);
    }
    QCOMPARE(parent->nHeight, 11);
    QCOMPARE(Blocks::DB::instance()->headerChain().Tip(), parent);
    assert(b8b);
    QCOMPARE(b8b->nHeight, 9);
    QCOMPARE(b8b->pprev->pprev, b6);

    b8b->nStatus |= BLOCK_FAILED_VALID;
    bool changed = Blocks::DB::instance()->appendHeader(b8b);
    QVERIFY(changed);
    QCOMPARE(Blocks::DB::instance()->headerChain().Tip(), b9);
    QCOMPARE(Blocks::DB::instance()->headerChainTips().size(), 2);
}


void TestBlocksDB::addImpliedInvalid()
{
    /*
     * Starting with;
     *   x x x x
     * And then adding an item a3 that would create;
     *   x x x x a1 a2 a3
     * requires me to check all new items for validity, to see if they have been marked as failing.
     * If one is failing, then all are.
     */

    std::vector<FastBlock> blocks = bv->appendChain(10);
    auto * const x = Blocks::DB::instance()->headerChain().Tip();
    QCOMPARE(x->nHeight, 10);

    uint256 hashes[3];
    CBlockIndex a1;
    a1.nHeight = 11;
    a1.pprev = x;
    a1.phashBlock = &hashes[0];
    a1.nChainWork = x->nChainWork + 0x10;
    a1.nStatus = BLOCK_FAILED_VALID;
    CBlockIndex a2;
    a2.nChainWork = a1.nChainWork + 0x10;
    a2.nHeight = a1.nHeight + 1;
    a2.pprev = &a1;
    a2.phashBlock = &hashes[1];
    a2.nStatus = BLOCK_FAILED_CHILD;
    CBlockIndex a3;
    a3.nChainWork = a2.nChainWork + 0x10;
    a3.nHeight = a2.nHeight + 1;
    a3.pprev = &a2;
    a3.phashBlock = &hashes[2];
    a3.nStatus = BLOCK_FAILED_CHILD;

    Blocks::DB::instance()->appendHeader(&a3);
    QCOMPARE(Blocks::DB::instance()->headerChain().Tip(), x);
}
