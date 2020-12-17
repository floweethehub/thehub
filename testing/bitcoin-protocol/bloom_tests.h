/*
 * This file is part of the Flowee project
 * Copyright (C) 2012-2015 The Bitcoin Core developers
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
#ifndef BLOOM_TESTS_H
#define BLOOM_TESTS_H

#include <common/TestFloweeEnvPlusNet.h>

#include <string>
#include <vector>

class TestBloom : public TestFloweeEnvPlusNet
{
    Q_OBJECT
private slots:
    void bloom_create_insert_serialize();
    void bloom_create_insert_serialize_with_tweak();
    void bloom_create_insert_key();
    void bloom_match();
    void merkle_block_1();
    void merkle_block_2();
    void merkle_block_2_reversed();
    void merkle_block_2_with_update_none();
    void merkle_block_3_and_serialize();
    void merkle_block_4();
    void merkle_block_4_test_p2pubkey_only();
    void merkle_block_4_test_update_none();
    void rolling_bloom();
};

#endif
