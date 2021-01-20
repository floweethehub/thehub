/*
 * This file is part of the Flowee project
 * Copyright (c) 2015 The Bitcoin Core developers
 * Copyright (C) 2017-2021 Tom Zander <tom@flowee.org>
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

#ifndef FLOWEE_TEST_TEST_MAIN_H
#define FLOWEE_TEST_TEST_MAIN_H

#include <chainparamsbase.h>
#include <primitives/key.h>
#include <primitives/pubkey.h>
#include <txmempool.h>
#include <Application.h>
#include <validation/Engine.h>
#include <primitives/FastBlock.h>
#include <primitives/transaction.h>

#include <boost/filesystem.hpp>


/** Basic testing setup.
 * This just configures logging and chain parameters.
 */
struct BasicTestingSetup {
    ECCVerifyHandle globalVerifyHandle;

    BasicTestingSetup(const std::string& chainName = CBaseChainParams::MAIN);
    ~BasicTestingSetup();

    const char *currentTestName() {
        return "test";
    }
};

class MockBlockValidation : public Validation::Engine {
public:
    MockBlockValidation();
    ~MockBlockValidation();

    void initSingletons();
    FastBlock createBlock(CBlockIndex *parent, const CScript& scriptPubKey, const std::vector<CTransaction>& txns = std::vector<CTransaction>()) const;
    /// short version of the above
    FastBlock createBlock(CBlockIndex *parent);

    /**
     * @brief appendGenesis creates the standard reg-test genesis and appends.
     * This will only succeed if the current chain (Params()) is REGTEST
     */
    void appendGenesis();


    enum OutputType {
        EmptyOutScript,
        StandardOutScript,
        FullOutScript // full p2pkh output script
    };

    /**
     * @brief Append a list of blocks to the block-validator and wait for them to be validated.
     * @param blocks the amount of blocks to add to the blockchain-tip.
     * @param coinbaseKey [out] an empty key we will initialize and use as coinbase.
     * @param out one of the OutputType members.
     */
    std::vector<FastBlock> appendChain(int blocks, CKey &coinbaseKey, OutputType out = StandardOutScript);

    inline std::vector<FastBlock> appendChain(int blocks, OutputType out = StandardOutScript) {
        CKey key;
        return appendChain(blocks, key, out);
    }

    /**
     * @brief This creates a chain of blocks on top of a random index.
     * @param parent the index that is to be extended
     * @param blocks the amount of blocks to build.
     * @return The full list of blocks.
     * This method doesn't add the blocks, use appendChain() for that.
     */
    std::vector<FastBlock> createChain(CBlockIndex *parent, int blocks) const;

    CTxMemPool mp;
};


/** Testing setup that configures a complete environment.
 * Included are data directory, coins database, script check threads
 * and wallet (if enabled) setup.
 */
struct TestingSetup: public BasicTestingSetup {
    MockBlockValidation bv;
    boost::filesystem::path pathTemp;

    enum BlocksDb {
        BlocksDbInMemory,
        BlocksDbOnDisk
    };

    TestingSetup(const std::string& chainName = CBaseChainParams::REGTEST);
    ~TestingSetup();
};

struct MainnetTestingSetup : TestingSetup {
    MainnetTestingSetup() : TestingSetup(CBaseChainParams::MAIN) {}
};

class CTxMemPoolEntry;
class CTxMemPool;

struct TestMemPoolEntryHelper
{
    // Default values
    int64_t nFee;
    int64_t nTime;
    double dPriority;
    unsigned int nHeight;
    bool hadNoDependencies;
    bool spendsCoinbase;
    LockPoints lp;

    TestMemPoolEntryHelper() :
        nFee(0), nTime(0), dPriority(0.0), nHeight(1),
        hadNoDependencies(false), spendsCoinbase(false) { }
    
    CTxMemPoolEntry FromTx(CMutableTransaction &tx, CTxMemPool *pool = NULL);

    // Change the default value
    TestMemPoolEntryHelper &Fee(int64_t _fee) { nFee = _fee; return *this; }
    TestMemPoolEntryHelper &Time(int64_t _time) { nTime = _time; return *this; }
    TestMemPoolEntryHelper &Priority(double _priority) { dPriority = _priority; return *this; }
    TestMemPoolEntryHelper &Height(unsigned int _height) { nHeight = _height; return *this; }
    TestMemPoolEntryHelper &HadNoDependencies(bool _hnd) { hadNoDependencies = _hnd; return *this; }
    TestMemPoolEntryHelper &SpendsCoinbase(bool _flag) { spendsCoinbase = _flag; return *this; }
};

class MockApplication : public Application
{
public:
    MockApplication() = delete;

    inline static void doInit() {
        static_cast<MockApplication*>(Application::instance())->pub_init();
    }
    inline static void doStartThreads() {
        static_cast<MockApplication*>(Application::instance())->pub_startThreads();
    }
    inline static void setValidationEngine(Validation::Engine *bv) {
        static_cast<MockApplication*>(Application::instance())->replaceValidationEngine(bv);
    }

protected:
    inline void pub_init() {
        init();
    }
    inline void pub_startThreads() {
        startThreads();
    }
    inline void replaceValidationEngine(Validation::Engine *bv) {
        m_validationEngine.release();
        m_validationEngine.reset(bv);
    }
};

#endif
