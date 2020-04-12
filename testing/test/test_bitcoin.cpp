/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2015 The Bitcoin Core developers
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
#include <chain.h>
#include <chainparams.h>
#include <merkle.h>
#include <main.h>
#include <primitives/key.h>
#include <net.h>
#include <random.h>
#include <UiInterface.h>
#include <interfaces/validationinterface.h>
#include <utxo/UnspentOutputDatabase.h>
#include <script/sigcache.h>
#include <server/serverutil.h>
#include <BlocksDB.h>
#ifdef ENABLE_WALLET
# include <wallet/wallet.h>
CWallet* pwalletMain;
#endif

#include <boost/test/included/unit_test.hpp>

CClientUIInterface uiInterface; // Declared but not defined in UiInterface.h

UnspentOutputDatabase *g_utxo = nullptr;

extern void noui_connect();

BasicTestingSetup::BasicTestingSetup(const std::string& chainName)
{
    Log::Manager::instance()->loadDefaultTestSetup(std::bind(&BasicTestingSetup::currentTestName, this));
    ECC_Start();
    SetupEnvironment();
    SetupNetworking();
    InitSignatureCache();
    mapArgs["-checkblockindex"] = "1";
    SelectParams(chainName);
    noui_connect();
    MockApplication::doStartThreads();
    MockApplication::doInit();
}



BasicTestingSetup::~BasicTestingSetup()
{
    ECC_Stop();
    Application::quit(0);
}

TestingSetup::TestingSetup(const std::string& chainName) : BasicTestingSetup(chainName)
{
#ifdef ENABLE_WALLET
    bitdb.MakeMock();
#endif
    ClearDatadirCache();
    pathTemp = boost::filesystem::temp_directory_path() / strprintf("test_flowee_%lu_%i", (unsigned long)GetTime(), (int)(GetRand(100000)));
    boost::filesystem::create_directories(pathTemp / "regtest/blocks/index");
    boost::filesystem::create_directories(pathTemp / "blocks/index");
    mapArgs["-datadir"] = pathTemp.string();
    Blocks::DB::createTestInstance(1<<20);
    UnspentOutputDatabase::setSmallLimits();
    g_utxo = new UnspentOutputDatabase(Application::instance()->ioService(), GetDataDir(true) / "unspent");

    bv.initSingletons();
    bv.appendGenesis();
    MockApplication::setValidationEngine(&bv);

#ifdef ENABLE_WALLET
    bool fFirstRun;
    pwalletMain = new CWallet("wallet.dat");
    pwalletMain->LoadWallet(fFirstRun);
    ValidationNotifier().addListener(pwalletMain);
#endif

    RegisterNodeSignals(GetNodeSignals());
}

TestingSetup::~TestingSetup()
{
    MockApplication::setValidationEngine(nullptr);
    bv.shutdown();
    Blocks::Index::unload();

    UnregisterNodeSignals(GetNodeSignals());
    ValidationNotifier().removeAll();
#ifdef ENABLE_WALLET
    delete pwalletMain;
    pwalletMain = NULL;
#endif
    UnloadBlockIndex();
    delete g_utxo;
#ifdef ENABLE_WALLET
    bitdb.Flush(true);
    bitdb.Reset();
#endif
    boost::filesystem::remove_all(pathTemp);
}


CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(CMutableTransaction &tx, CTxMemPool *pool)
{
    CTransaction txn(tx);
    bool hasNoDependencies = pool ? pool->HasNoInputsOf(tx) : hadNoDependencies;
    // Hack to assume either its completely dependent on other mempool txs or not at all
    CAmount inChainValue = hasNoDependencies ? txn.GetValueOut() : 0;

    return CTxMemPoolEntry(txn, nFee, nTime, dPriority, nHeight,
                           hasNoDependencies, inChainValue, spendsCoinbase, lp);
}

void Shutdown(void* parg)
{
    exit(0);
}

void StartShutdown()
{
    exit(0);
}

bool ShutdownRequested()
{
    return false;
}


////////////////////////////////////

MockBlockValidation::MockBlockValidation()
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
    if (out == StandardOutScript) {
        scriptPubKey << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    } else if (out == FullOutScript) {
        scriptPubKey << OP_DUP << OP_HASH160 << ToByteVector(coinbaseKey.GetPubKey().GetID())
                     << OP_EQUALVERIFY << OP_CHECKSIG;
    }
    waitValidationFinished();
    const bool allowFullChecks = Params().NetworkIDString() == "regtest";
    for (int i = 0; i < blocks; i++)
    {
        CBlockIndex *tip = blockchain()->Tip();
        assert(tip);
        auto block = createBlock(tip, scriptPubKey);
        answer.push_back(block);
        auto future = addBlock(block, Validation::SaveGoodToDisk, nullptr);
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
