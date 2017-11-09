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

#include "test_bitcoin.h"

#include <BlocksDB.h>
#include <chain.h>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(blocksdb, TestingSetup)


static bool contains(const std::list<CBlockIndex*> &haystack, CBlockIndex *needle)
{
    // and the stl designers wonder why people say C++ is too verbose and hard to use.
    // would it really have hurt us if they had a std::list<T>::contains(T t) method?
    return (std::find(haystack.begin(), haystack.end(), needle) != haystack.end());
}

BOOST_AUTO_TEST_CASE(headersChain)
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

        BOOST_CHECK_EQUAL(changed, true);
        BOOST_CHECK_EQUAL(db->headerChain().Tip(), &root);
        BOOST_CHECK_EQUAL(db->headerChainTips().size(), 1);
        BOOST_CHECK_EQUAL(db->headerChainTips().front(), &root);

        changed = db->appendHeader(&b1);
        BOOST_CHECK_EQUAL(changed, true);
        BOOST_CHECK_EQUAL(db->headerChain().Tip(), &b1);
        BOOST_CHECK_EQUAL(db->headerChainTips().size(), 1);
        BOOST_CHECK_EQUAL(db->headerChainTips().front(), &b1);

        changed = db->appendHeader(&b4);
        BOOST_CHECK_EQUAL(changed, true);
        BOOST_CHECK_EQUAL(db->headerChain().Tip(), &b4);
        BOOST_CHECK_EQUAL(db->headerChain().Height(), 4);
        BOOST_CHECK_EQUAL(db->headerChainTips().size(), 1);
        BOOST_CHECK_EQUAL(db->headerChainTips().front(), &b4);

        changed = db->appendHeader(&bp3);
        BOOST_CHECK_EQUAL(changed, false);
        BOOST_CHECK_EQUAL(db->headerChain().Tip(), &b4);
        BOOST_CHECK_EQUAL(db->headerChain().Height(), 4);
        BOOST_CHECK_EQUAL(db->headerChainTips().size(), 2);
        BOOST_CHECK(contains(db->headerChainTips(), &b4));
        BOOST_CHECK(contains(db->headerChainTips(), &bp3));

        changed = db->appendHeader(&bp4);
        BOOST_CHECK_EQUAL(changed, true);
        BOOST_CHECK_EQUAL(db->headerChain().Tip(), &bp4);
        BOOST_CHECK_EQUAL(db->headerChain().Height(), 4);
        BOOST_CHECK_EQUAL(db->headerChainTips().size(), 2);
        BOOST_CHECK(contains(db->headerChainTips(), &b4));
        BOOST_CHECK(contains(db->headerChainTips(), &bp4));


        BOOST_CHECK_EQUAL(db->headerChain()[0], &root);
        BOOST_CHECK_EQUAL(db->headerChain()[1], &b1);
        BOOST_CHECK_EQUAL(db->headerChain()[2], &b2);
        BOOST_CHECK_EQUAL(db->headerChain()[3], &bp3);
        BOOST_CHECK_EQUAL(db->headerChain()[4], &bp4);
    }

    {
        Blocks::DB::createTestInstance(100);
        Blocks::DB *db = Blocks::DB::instance();
        bool changed = db->appendHeader(&bp3);
        BOOST_CHECK_EQUAL(changed, true);
        BOOST_CHECK_EQUAL(db->headerChain().Tip(), &bp3);
        BOOST_CHECK_EQUAL(db->headerChain().Height(), 3);
        BOOST_CHECK_EQUAL(db->headerChainTips().size(), 1);
        BOOST_CHECK_EQUAL(db->headerChainTips().front(), &bp3);

        changed = db->appendHeader(&b3);
        BOOST_CHECK_EQUAL(changed, false);
        BOOST_CHECK_EQUAL(db->headerChain().Tip(), &bp3);
        BOOST_CHECK_EQUAL(db->headerChain().Height(), 3);
        BOOST_CHECK_EQUAL(db->headerChainTips().size(), 2);
        BOOST_CHECK(contains(db->headerChainTips(), &bp3));
        BOOST_CHECK(contains(db->headerChainTips(), &b3));

        BOOST_CHECK_EQUAL(db->headerChain()[0], &root);
        BOOST_CHECK_EQUAL(db->headerChain()[1], &b1);
        BOOST_CHECK_EQUAL(db->headerChain()[2], &b2);
        BOOST_CHECK_EQUAL(db->headerChain()[3], &bp3);
    }
    {
        Blocks::DB::createTestInstance(100);
        Blocks::DB *db = Blocks::DB::instance();
        bool changed = db->appendHeader(&b3);
        BOOST_CHECK_EQUAL(changed, true);
        changed = db->appendHeader(&b2);
        BOOST_CHECK_EQUAL(changed, false);
        BOOST_CHECK_EQUAL(db->headerChain().Tip(), &b3);
        BOOST_CHECK_EQUAL(db->headerChain().Height(), 3);
        BOOST_CHECK_EQUAL(db->headerChainTips().size(), 1);
        BOOST_CHECK_EQUAL(db->headerChainTips().front(), &b3);
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
        BOOST_CHECK_EQUAL(changed, false);
        BOOST_CHECK_EQUAL(db->headerChain().Tip(), &b3);
        BOOST_CHECK_EQUAL(db->headerChain().Height(), 3);
        BOOST_CHECK_EQUAL(db->headerChainTips().size(), 2);
    }
}

BOOST_AUTO_TEST_CASE(headersChain2)
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
        BOOST_CHECK_EQUAL(changed, true);
        BOOST_CHECK_EQUAL(db->headerChain().Tip(), &b2);
        BOOST_CHECK_EQUAL(db->headerChain().Height(), 2);
        BOOST_CHECK_EQUAL(db->headerChainTips().size(), 1);
        BOOST_CHECK_EQUAL(db->headerChainTips().front(), &b2);
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
        BOOST_CHECK_EQUAL(changed, true);
        BOOST_CHECK_EQUAL(db->headerChain().Tip(), &b1);
        BOOST_CHECK_EQUAL(db->headerChain().Height(), 1);
        BOOST_CHECK_EQUAL(db->headerChainTips().size(), 1);
        BOOST_CHECK_EQUAL(db->headerChainTips().front(), &b1);
    }
}

BOOST_AUTO_TEST_SUITE_END()
