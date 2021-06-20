/*
 * This file is part of the Flowee project
 * Copyright (C) 2017 Calin Culianu <calin.culianu@gmail.com>
 * Copyright (C) 2017 Tom Zander <tom@flowee.org>
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

#include <BlocksDB.h>
#include <BlocksDB_p.h>
#include <main.h>
#include <undo.h>
#include <util.h>
#include <streaming/BufferPool.h>
#include <primitives/FastBlock.h>
#include <primitives/FastUndoBlock.h>

#include <boost/test/unit_test.hpp>


BOOST_FIXTURE_TEST_SUITE(blocksdb_mapfile_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(mapFile_write)
{
    // There likely is one already, for the genesis block, lets avoid interacting with it and just force a new file.
    BOOST_CHECK_EQUAL(vinfoBlockFile.size(), 1);
    vinfoBlockFile[0].nSize = MAX_BLOCKFILE_SIZE - 107;

    Blocks::DB *db = Blocks::DB::instance();
    Streaming::BufferPool pool;
    pool.reserve(100);
    for (int i = 0; i < 100; ++i) {
        pool.begin()[i] = static_cast<char>(i);
    }
    FastBlock block(pool.commit(100));
    BOOST_CHECK_EQUAL(block.size(), 100);
    BOOST_CHECK_EQUAL(block.blockVersion(), 0x3020100);
    CDiskBlockPos pos;
    {
        FastBlock newBlock = db->writeBlock(block, pos);
        BOOST_CHECK_EQUAL(newBlock.blockVersion(), 0x3020100);
        BOOST_CHECK_EQUAL(newBlock.size(), 100);
        BOOST_CHECK_EQUAL(pos.nFile, 1);
        BOOST_CHECK_EQUAL(pos.nPos, 8);
    }
    {
        FastBlock block2 = db->loadBlock(CDiskBlockPos(1, 8));
        BOOST_CHECK_EQUAL(block2.size(), 100);
        BOOST_CHECK_EQUAL(block2.blockVersion(), 0x3020100);
    }

    // add a second block
    pool.reserve(120);
    for (int i = 0; i < 120; ++i) {
        pool.begin()[i] = static_cast<char>(i + 1);
    }
    FastBlock block2(pool.commit(120));
    BOOST_CHECK_EQUAL(block2.size(), 120);
    BOOST_CHECK_EQUAL(block2.blockVersion(), 0x4030201);

    {
        FastBlock newBlock = db->writeBlock(block2, pos);
        BOOST_CHECK_EQUAL(newBlock.size(), 120);
        BOOST_CHECK_EQUAL(pos.nFile, 1);
        BOOST_CHECK_EQUAL(pos.nPos, 116);
        BOOST_CHECK_EQUAL(newBlock.blockVersion(), 0x4030201);
    }
    {
        FastBlock block3 = db->loadBlock(CDiskBlockPos(1, 8));
        BOOST_CHECK_EQUAL(block3.size(), 100);
        BOOST_CHECK_EQUAL(block3.blockVersion(), 0x3020100);
        BOOST_CHECK_EQUAL(block3.data().begin()[99], (char) 99);

        FastBlock block4 = db->loadBlock(CDiskBlockPos(1, 116));
        BOOST_CHECK_EQUAL(block4.size(), 120);
        BOOST_CHECK_EQUAL(block4.blockVersion(), 0x4030201);
        BOOST_CHECK(block4.data().begin()[119] == 120);
    }
}

BOOST_AUTO_TEST_SUITE_END()
