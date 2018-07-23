/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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
#include "BlocksDB.h"
#include "UnspentOutputData.h"
#include "chain.h"

#include <primitives/FastBlock.h>
#include <primitives/FastTransaction.h>

UnspentOutputData::UnspentOutputData(const UnspentOutput &uo)
    : m_uo(uo),
    m_txVer(-1),
    m_outputValue(-1)
{
    if (!uo.isValid())
        return;

    Blocks::DB *blockDb = Blocks::DB::instance();
    auto blockIndex = blockDb->headerChain()[uo.blockHeight()];
    if (blockIndex == nullptr)
        return;

    auto block = blockDb->loadBlock(blockIndex->GetBlockPos());
    if (!block.isFullBlock())
        return;
    assert(block.size() > uo.offsetInBlock());

    std::int64_t outputValue = -1;
    Tx::Iterator iter(block, uo.offsetInBlock());
    int outputs = 0;
    auto type = iter.next();
    while (type != Tx::End) {
        if (type == Tx::TxVersion) {
            m_txVer = iter.intData();
        } else if (type == Tx::OutputValue) {
            outputValue = static_cast<std::int64_t>(iter.longData());
        } else if (type == Tx::OutputScript) {
            if (outputs++ == outIndex()) {
                m_outputValue = outputValue;
                m_outputScript = iter.byteData();
                break;
            }
        }
        type = iter.next();
    }
}
