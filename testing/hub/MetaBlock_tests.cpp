/*
 * This file is part of the Flowee project
 * Copyright (C) 2021 Tom Zander <tom@flowee.org>
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
#include "MetaBlock_tests.h"

#include <BlockMetaData.h>
#include <streaming/BufferPool.h>
#include <primitives/FastBlock.h>

TestMetaBlock::TestMetaBlock()
{
}

void TestMetaBlock::testCreation()
{
    QFile input(":/blockdata");
    Streaming::BufferPool pool;
    bool ok = input.open(QIODevice::ReadOnly);
    QVERIFY(ok);
    auto size = input.read(pool.begin(), pool.capacity());
    FastBlock block(pool.commit(size));
    QVERIFY(block.createHash()
            == uint256S("0x00000000000000000560372e0caadc38c56cde6c4aaae03287a6898e643e5b8a"));


    std::vector<std::unique_ptr<std::deque<std::int32_t> > > dummy;
    dummy.resize(1);
    dummy[0].reset(new std::deque<std::int32_t>());
    dummy[0]->push_back(8475); // first one should go to the first real transaction (not coinbase);
    BlockMetaData md = BlockMetaData::parseBlock(13451, block, dummy, pool);
    QCOMPARE(md.blockHeight(), 13451);
    QCOMPARE(md.ctorSorted(), true);
    QCOMPARE(md.txCount(), 94);
    auto coinbase = md.first();
    QCOMPARE(coinbase->offsetInBlock, 81);
    QVERIFY(uint256(coinbase->txid) == uint256S("0x39d00f962892cc5b3fc013ab3f02b7f9381d8ff1ea591bae81e8272211230fbd"));
    QCOMPARE(coinbase->fees, 0);
    auto nextTx = coinbase->next();
    QCOMPARE(nextTx->offsetInBlock, 248);
    QVERIFY(uint256(nextTx->txid) == uint256S("0x00f3f68b87882ade82461b1185ef512434fbdadd91bf14edc1dc9528257fe0e9"));
    QCOMPARE(nextTx->fees, 8475);
    QCOMPARE(nextTx->next()->fees, 0);

    BlockMetaData md2(md.data());
    QCOMPARE(md2.blockHeight(), 13451);
    QCOMPARE(md2.ctorSorted(), true);
    QCOMPARE(md2.txCount(), 94);
    coinbase = md2.first();
    QCOMPARE(coinbase->offsetInBlock, 81);
    QVERIFY(uint256(coinbase->txid) == uint256S("0x39d00f962892cc5b3fc013ab3f02b7f9381d8ff1ea591bae81e8272211230fbd"));
    QCOMPARE(coinbase->fees, 0);
    nextTx = coinbase->next();
    QCOMPARE(nextTx->offsetInBlock, 248);
    QVERIFY(uint256(nextTx->txid) == uint256S("0x00f3f68b87882ade82461b1185ef512434fbdadd91bf14edc1dc9528257fe0e9"));
    QCOMPARE(nextTx->fees, 8475);
    QCOMPARE(nextTx->next()->fees, 0);

    try {
        md2.tx(0);
        md2.tx(50);
        md2.tx(93);
    } catch (...) {
        QFAIL("Should not throw");
    }
    try {
        md2.tx(94);
        QFAIL("Out of bounds, should have thrown");
    } catch (...) { }
}
