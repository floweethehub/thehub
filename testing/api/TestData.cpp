/*
 * This file is part of the Flowee project
 * Copyright (C) 202l Tom Zander <tom@flowee.org>
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
#include "TestData.h"

std::pair<Tx, Tx> TestData::createDoubleSpend(Streaming::BufferPool *pool)
{
    // two transactions that both spend the first output of the first (non-coinbase) tx on block 115
    // The spend TO the above addresses.
    pool->reserve(384);
    pool->writeHex("0x01000000010b9d14b709aa59bd594edca17db2951c6660ebc8daa31ceae233a5550314f158000000006b483045022100b34a120e69bc933ae16c10db0f565cb2da1b80a9695a51707e8a80c9aa5c22bf02206c390cb328763ab9ab2d45f874d308af2837d6d8cfc618af76744b9eeb69c3934121022708a547a1d14ba6df79ec0f4216eeec65808cf0a32f09ad1cf730b44e8e14a6ffffffff01faa7be00000000001976a9148438266ad57aa9d9160e99a046e39027e4fb6b2a88ac00000000");
    Tx tx1(pool->commit());

    pool->writeHex("0x01000000010b9d14b709aa59bd594edca17db2951c6660ebc8daa31ceae233a5550314f158000000006b483045022100d9d22406611228d64e6b674de8b16e802f8f789f8338130506c7741cdae9116602202dc63a4f5f9e750eec9dfc1557469bda43d3491b358484e5c25992a381048a494121022708a547a1d14ba6df79ec0f4216eeec65808cf0a32f09ad1cf730b44e8e14a6ffffffff01ea80be00000000001976a914a449b2bf8b8092a810ee3b4ba102037bf4b96d2288ac00000000");
    Tx tx2(pool->commit());

    return std::pair<Tx,Tx>(tx1, tx2);
}
