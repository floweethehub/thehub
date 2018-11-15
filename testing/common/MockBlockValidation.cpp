/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2015 The Bitcoin Core developers
 * Copyright (C) 2017-2018 Tom Zander <tomz@freedommail.ch>
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

#include "MockBlockValidation.h"

#include <chain.h>
#include <chainparams.h>
#include <consensus/merkle.h>
#include <main.h>

MockBlockValidation::MockBlockValidation()
    : mp(minFee)
{
}

void MockBlockValidation::initSingletons()
{
    // set all the stuff that has been created in the Fixture (TestingSetup::TestingSetup())
    mp.setUtxo(g_utxo);
    setMempool(&mp);
    chainActive.SetTip(nullptr);
    setBlockchain(&chainActive);
}

MockBlockValidation::~MockBlockValidation()
{
    g_utxo = 0;
}

FastBlock MockBlockValidation::createBlock(CBlockIndex *parent, const CScript& scriptPubKey, const std::vector<CTransaction>& txns) const
{
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vout.resize(1);
    coinbase.vin[0].scriptSig = CScript() << (parent->nHeight + 1) << OP_0;
    coinbase.vout[0].nValue = 50 * COIN;
    coinbase.vout[0].scriptPubKey = scriptPubKey;
    // Make sure the coinbase is big enough. (since 20181115 HF we require a min 100bytes tx size)
    const uint32_t coinbaseSize = ::GetSerializeSize(coinbase, SER_NETWORK, PROTOCOL_VERSION);
    if (coinbaseSize < 100)
        coinbase.vin[0].scriptSig << std::vector<uint8_t>(100 - coinbaseSize - 1);

    CBlock block;
    block.vtx.push_back(coinbase);
    block.nVersion = 4;
    block.hashPrevBlock = *parent->phashBlock;
    block.nTime = parent->nTime + 2;
    block.nNonce = 0;

    // don't call this in testNet, it will crash due to that null
    block.nBits = GetNextWorkRequired(parent, nullptr, Params().GetConsensus());

    for (const CTransaction &tx : txns) {
        block.vtx.push_back(tx);
    }
    block.hashMerkleRoot = BlockMerkleRoot(block);
    const bool mine = Params().NetworkIDString() == "regtest";
    do {
        ++block.nNonce;
    } while (mine && !CheckProofOfWork(block.GetHash(), block.nBits, Params().GetConsensus()));

    return FastBlock::fromOldBlock(block);
}

FastBlock MockBlockValidation::createBlock(CBlockIndex *parent)
{
    CKey coinbaseKey;
    coinbaseKey.MakeNewKey();
    CScript scriptPubKey;
    scriptPubKey <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    return createBlock(parent, scriptPubKey);
}

void MockBlockValidation::appendGenesis()
{
    addBlock(FastBlock::fromOldBlock(Params().GenesisBlock()), Validation::SaveGoodToDisk);
    waitValidationFinished();
}

std::vector<FastBlock> MockBlockValidation::appendChain(int blocks, CKey &coinbaseKey, OutputType out)
{
    std::vector<FastBlock> answer;
    answer.reserve(blocks);
    coinbaseKey.MakeNewKey();
    CScript scriptPubKey;
    if (out == StandardOutScript)
        scriptPubKey <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    waitValidationFinished();
    const bool allowFullChecks = Params().NetworkIDString() == "regtest";
    for (int i = 0; i < blocks; i++)
    {
        CBlockIndex *tip = blockchain()->Tip();
        assert(tip);
        auto block = createBlock(tip, scriptPubKey);
        answer.push_back(block);
        auto future = addBlock(block, Validation::SaveGoodToDisk, 0);
        future.setCheckPoW(allowFullChecks);
        future.setCheckMerkleRoot(allowFullChecks);
        future.start();
        future.waitUntilFinished();
    }
    return answer;
}

std::vector<FastBlock> MockBlockValidation::createChain(CBlockIndex *parent, int blocks) const
{
    CKey coinbaseKey;
    coinbaseKey.MakeNewKey();
    CScript scriptPubKey = CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CBlockIndex dummy;
    dummy.nTime = parent->nTime;
    dummy.phashBlock = parent->phashBlock;
    uint256 dummySha;
    uint32_t bits = parent->nBits;

    std::vector<FastBlock> answer;
    for (int i = 0; i < blocks; ++i) {
        dummy.nHeight = parent->nHeight + i;
        dummy.nTime += 10;
        dummy.nBits = bits;
        FastBlock block = createBlock(&dummy, scriptPubKey);
        bits = block.bits();
        answer.push_back(block);
        dummySha = block.createHash();
        dummy.phashBlock = &dummySha;
    }
    return answer;
}
