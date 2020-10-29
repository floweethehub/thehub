/*
 * This file is part of the Flowee project
 * Copyright (c) 2010 Satoshi Nakamoto
 * Copyright (c) 2009-2015 The Bitcoin Core developers
 * Copyright (C) 2016-2020 Tom Zander <tomz@freedommail.ch>
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

#include "chainparams.h"

#include <SettingsDefaults.h>
#include <util.h>
#include <merkle.h>
#include <utilstrencodings.h>

#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()

#include "chainparamsseeds.h"


static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = nVersion;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(txNew);
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks";
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 227931;
        consensus.BIP65Height = 388381; // CHECKLOCKTIMEVERIFY
        consensus.BIP66Height = 363725; // DERSIG
        consensus.BIP68Height = 419328; // sequence locks & CHECKSEQUENCEVERIFY
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing

        // The half life for the ASERT DAA. For every (nASERTHalfLife) seconds behind schedule the blockchain gets,
        // difficulty is cut in half. Doubled if blocks are ahead of schedule.
        // Two days
        consensus.nASERTHalfLife = 2 * 24 * 60 * 60;

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xf9;
        pchMessageStart[1] = 0xbe;
        pchMessageStart[2] = 0xb4;
        pchMessageStart[3] = 0xd9;

        pchMessageStartCash[0] = 0XE3;
        pchMessageStartCash[1] = 0XE1;
        pchMessageStartCash[2] = 0XF3;
        pchMessageStartCash[3] = 0XE8;

        nDefaultPort = Settings::DefaultMainnetPort;
        nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock(1231006505, 2083236893, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        // Timestamps for forking consensus rule changes:
        assert(consensus.hashGenesisBlock == uint256S("0x000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"));
        assert(genesis.hashMerkleRoot == uint256S("0x4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

        vSeeds.push_back(CDNSSeedData("flowee", "seed.flowee.cash"));
        vSeeds.push_back(CDNSSeedData("bchd", "seed.bchd.cash"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        consensus.hf201708Height = 478559;
        consensus.hf201711Height = 504031;
        consensus.hf201805Height = 530356;
        consensus.hf201811Height = 556767;
        consensus.hf201905Height = 582680;
        consensus.hf201911Height = 609135;
        consensus.hf202005Height = 635258;
        consensus.hf202011Time = 1605441600;

        checkpointData = CCheckpointData {
            boost::assign::map_list_of
            ( 11111, uint256S("0000000069e244f73d78e8fd29ba2fd2ed618bd6fa2ee92559f542fdb26e7c1d"))
            ( 33333, uint256S("000000002dd5588a74784eaa7ab0507a18ad16a236e7b1ce69f00d7ddfb5d0a6"))
            ( 74000, uint256S("0000000000573993a3c9e41ce34471c079dcf5f52a0e824a81e7f953b8661a20"))
            (105000, uint256S("00000000000291ce28027faea320c8d2b054b2e0fe44a773f3eefb151d6bdc97"))
            (134444, uint256S("00000000000005b12ffd4cd315cd34ffd4a594f430ac814c91184a0d42d2b0fe"))
            (168000, uint256S("000000000000099e61ea72015e79632f216fe6cb33d7899acb35b75c8303b763"))
            (193000, uint256S("000000000000059f452a5f7340de6682a977387c17010ff6e6c3bd83ca8b1317"))
            (210000, uint256S("000000000000048b95347e83192f69cf0366076336c639f9b7228e9ba171342e"))
            (216116, uint256S("00000000000001b4f4b433e81ee46494af945cf96014816a4e2370f11b23df4e"))
            (225430, uint256S("00000000000001c108384350f74090433e7fcf79a606b8e797f065b130575932"))
            (250000, uint256S("000000000000003887df1f29024b06fc2200b55f8af8f35453d7be294df2d214"))
            (279000, uint256S("0000000000000001ae8c72a0b0c301f67e3afca10e819efa9041e458e9bd7e40"))
            (295000, uint256S("00000000000000004d9b4ef50f0f9d686fd69db2e03af35a100370c64632a983"))
            (478559, uint256S("000000000000000000651ef99cb9fcbe0dadde1d424bd9f15ff20136191a5eec"))
            (556767, uint256S("0000000000000000004626ff6e3b936941d341c5932ece4357eeccac44e6d56c"))
            (582680, uint256S("000000000000000001b4b8e36aec7d4f9671a47872cb9a74dc16ca398c7dcc18"))
            (609136, uint256S("000000000000000000b48bb207faac5ac655c313e41ac909322eaa694f5bc5b1"))
            (635259, uint256S("00000000000000000033dfef1fc2d6a5d5520b078c55193a9bf498c5b27530f7"))
            ,
            1573825449, // * UNIX timestamp of last checkpoint block
            281198294,  // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the new best hub.log lines)
            40000.0     // * estimated number of transactions per day after checkpoint
        };
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 21111;
        consensus.BIP65Height = 581885; // CHECKLOCKTIMEVERIFY
        consensus.BIP66Height = 330776; // DERSIG
        consensus.BIP68Height = 770112; // sequence locks & CHECKSEQUENCEVERIFY
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.nASERTHalfLife = 60 * 60;

        pchMessageStart[0] = 0x0B;
        pchMessageStart[1] = 0x11;
        pchMessageStart[2] = 0x09;
        pchMessageStart[3] = 0x07;

        pchMessageStartCash[0] = 0xF4;
        pchMessageStartCash[1] = 0xE5;
        pchMessageStartCash[2] = 0xF3;
        pchMessageStartCash[3] = 0xF4;

        nDefaultPort = Settings::DefaultTestnetPort;
        nMaxTipAge = 0x7fffffff;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1296688602, 414098458, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943"));
        assert(genesis.hashMerkleRoot == uint256S("0x4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("bchd", "testnet-seed.bchd.cash"));
        vSeeds.push_back(CDNSSeedData("bitcoinforks.org", "testnet-seed-bch.bitcoinforks.org"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        consensus.hf201708Height = 1155876;
        consensus.hf201711Height = 1188697;
        consensus.hf201805Height = 1267994;
        consensus.hf201811Height = 1267997;
        consensus.hf201905Height = 1303885;
        consensus.hf201911Height = 1341711;
        consensus.hf202005Height = 1378460;
        consensus.hf202011Time = GetArg("-axionactivationtime", 1605441600);

        checkpointData = CCheckpointData {
            boost::assign::map_list_of
            (546, uint256S("000000002a936ca763904c3c35fce2f3556c559c0214345d31b1bcebf76acb70"))
            (1155875, uint256S("00000000f17c850672894b9a75b63a1e72830bbd5f4" "c8889b5c1a80e7faef138"))
            (1188697, uint256S("0000000000170ed0918077bde7b4d36cc4c91be69fa" "09211f748240dabe047fb"))
            (1233070, uint256S("0000000000000253c6201a2076663cfe4722e4c75f537552cc4ce989d15f7cd5"))
            (1267997, uint256S("00000000000002773f8970352e4a3368a1ce6ef91eb606b64389b36fdbf1bd56"))
            (1303885, uint256S("00000000000000479138892ef0e4fa478ccc938fb94df862ef5bde7e8dee23d3"))
            (1341712, uint256S("00000000fffc44ea2e202bd905a9fbbb9491ef9e9d5a9eed4039079229afa35b"))
            ,
            1522608381, // * UNIX timestamp of last checkpoint block
            15052068,    // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain hub.log lines)
            300         // * estimated number of transactions per day after checkpoint
        };
    }
};

/**
 * Testnet (v4)
 */
class CTestNet4Params : public CChainParams {
public:
    CTestNet4Params() {
        strNetworkID = "test4";
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 2;
        consensus.BIP65Height = 3; // CHECKLOCKTIMEVERIFY
        consensus.BIP66Height = 4; // DERSIG
        consensus.BIP68Height = 5; // sequence locks & CHECKSEQUENCEVERIFY
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing

        // The half life for the ASERT DAA. For every (nASERTHalfLife) seconds behind schedule the blockchain gets,
        // difficulty is cut in half. Doubled if blocks are ahead of schedule.
        // One hour
        consensus.nASERTHalfLife = 60 * 60;

        pchMessageStart[0] = 0xcd;
        pchMessageStart[1] = 0x22;
        pchMessageStart[2] = 0xa7;
        pchMessageStart[3] = 0x92;

        pchMessageStartCash[0] = 0xe2;
        pchMessageStartCash[1] = 0xb7;
        pchMessageStartCash[2] = 0xda;
        pchMessageStartCash[3] = 0xaf;

        nDefaultPort = Settings::DefaultTestnet4Port;
        nMaxTipAge = 0x7fffffff;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1597811185, 114152193, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x000000001dd410c49a788668ce26751718cc797474d3152a5fc073dd44fd9f7b"));

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("toomim", "testnet4-seed-bch.toom.im"));
        vSeeds.push_back(CDNSSeedData("bitcoinforks.org", "testnet4-seed-bch.bitcoinforks.org"));
        vSeeds.push_back(CDNSSeedData("loping.net", "seed.tbch4.loping.net"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 196);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test4, pnSeed6_test4 + ARRAYLEN(pnSeed6_test4));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        consensus.hf201708Height = 6;
        consensus.hf201711Height = 3000;
        consensus.hf201805Height = 4000;
        consensus.hf201811Height = 4000;
        consensus.hf201905Height = 0;   // we use schnorr from the start.
        consensus.hf201911Height = 5000;
        consensus.hf202005Height = 0;   // sigop counting irrelevant on this chain.
        consensus.hf202011Time = GetArg("-axionactivationtime", 1605441600);

        checkpointData = CCheckpointData{
            boost::assign::map_list_of
            (0, uint256S("0x000000001dd410c49a788668ce26751718cc797474d3152a5fc073dd44fd9f7b"))
            (5677, uint256S("0x0000000019df558b6686b1a1c3e7aee0535c38052651b711f84eebafc0cc4b5e"))
            (9999, uint256S("0x00000000016522b7506939b23734bca7681c42a53997f2943ab4c8013936b419"))
            ,
            1602102194, // * UNIX timestamp of last checkpoint block
            11789,      // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain hub.log lines)
            1.3         // * estimated number of transactions per day after checkpoint
        };
    }
};

/**
 * Scaling Network
 */
class CScaleNetParams : public CChainParams {
public:
    CScaleNetParams() {
        strNetworkID = "scale";
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 2;
        consensus.BIP65Height = 3; // CHECKLOCKTIMEVERIFY
        consensus.BIP66Height = 4; // DERSIG
        consensus.BIP68Height = 5; // sequence locks & CHECKSEQUENCEVERIFY
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.nASERTHalfLife = 2 * 24 * 60 * 60;

        pchMessageStart[0] = 0xba;
        pchMessageStart[1] = 0xc2;
        pchMessageStart[2] = 0x2d;
        pchMessageStart[3] = 0xc4;

        pchMessageStartCash[0] = 0xc3;
        pchMessageStartCash[1] = 0xaf;
        pchMessageStartCash[2] = 0xe1;
        pchMessageStartCash[3] = 0xa2;

        nDefaultPort = Settings::DefaultScalenetPort;
        nMaxTipAge = 0x7fffffff;
        nPruneAfterHeight = 1000;

        //! Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis = CreateGenesisBlock(1598282438, -1567304284, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("00000000e6453dc2dfe1ffa19023f86002eb11dbb8e87d0291a4599f0430be52"));
        assert(genesis.hashMerkleRoot == uint256S("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("toom.im", "scalenet-seed-bch.toom.im"));
        vSeeds.push_back(CDNSSeedData("loping.net", "seed.sbch.loping.net"));
        vSeeds.push_back(CDNSSeedData("bitcoinforks.org", "scalenet-seed-bch.bitcoinforks.org"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 196);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_scalenet, pnSeed6_scalenet + ARRAYLEN(pnSeed6_scalenet));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        consensus.hf201708Height = 6;
        consensus.hf201711Height = 3000;
        consensus.hf201805Height = 4000;
        consensus.hf201811Height = 4000;
        consensus.hf201905Height = 5000;
        consensus.hf201911Height = 5000;
        consensus.hf202005Height = 0; // sigop counting irrelevant on this chain.
        consensus.hf202011Time = GetArg("-axionactivationtime", 1605441600);

        checkpointData = CCheckpointData{
            boost::assign::map_list_of
            (0, uint256S("0x00000000e6453dc2dfe1ffa19023f86002eb11dbb8e87d0291a4599f0430be52"))
            (45, uint256S("0x00000000d75a7c9098d02b321e9900b16ecbd552167e65683fe86e5ecf88b320"))
            ,
            0,          // * UNIX timestamp of last checkpoint block
            0,          // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain hub.log lines)
            0           // * estimated number of transactions per day after checkpoint
        };
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP34Height = -1; // BIP34 has not necessarily activated on regtest
        consensus.BIP65Height = 1; // CHECKLOCKTIMEVERIFY
        consensus.BIP66Height = 1; // DERSIG
        consensus.BIP68Height = 1; // sequence locks & CHECKSEQUENCEVERIFY
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.nASERTHalfLife = 0; // not used in regtest.

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;

        pchMessageStartCash[0] = 0xDA;
        pchMessageStartCash[1] = 0xB5;
        pchMessageStartCash[2] = 0xBF;
        pchMessageStartCash[3] = 0xFA;

        nMaxTipAge = 24 * 60 * 60;
        nDefaultPort = 18444;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1296688602, 2, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"));
        assert(genesis.hashMerkleRoot == uint256S("0x4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        consensus.hf201708Height = 0;
        consensus.hf201711Height = 0;
        consensus.hf201805Height = 1;
        consensus.hf201811Height = 9999999; // avoid invalidating my unit-test chain
        consensus.hf201905Height = 1;
        consensus.hf201911Height = 0;
        consensus.hf202005Height = 0;
        consensus.hf202011Time = 1;

        checkpointData = CCheckpointData {
            boost::assign::map_list_of
            ( 0, uint256S("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")),
            0,
            0,
            0
        };
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();
    }
};

namespace Static {
class Chains {
public:
    Chains() : m_currentParams(nullptr) {}
    CChainParams &main() {
        if (m_main.get() == nullptr)
            m_main.reset(new CMainParams());
        return *m_main.get();
    }
    CChainParams &regtest() {
        if (m_regtest.get() == nullptr)
            m_regtest.reset(new CRegTestParams());
        return *m_regtest.get();
    }
    CChainParams &testnet3() {
        if (m_testnet.get() == nullptr)
            m_testnet.reset(new CTestNetParams());
        return *m_testnet.get();
    }
    CChainParams &testnet4() {
        if (m_testnet4.get() == nullptr)
            m_testnet4.reset(new CTestNet4Params());
        return *m_testnet4.get();
    }
    CChainParams &scalenet() {
        if (m_scalenet.get() == nullptr)
            m_scalenet.reset(new CScaleNetParams());
        return *m_scalenet.get();
    }
    CChainParams &current() const {
        assert(m_currentParams);
        return *m_currentParams;
    }
    bool isSet() const {
        return m_currentParams != nullptr;
    }
    void setCurrent(CChainParams &params) {
        m_currentParams = &params;
    }

private:
    std::unique_ptr<CChainParams> m_main, m_testnet, m_testnet4, m_scalenet, m_regtest;
    CChainParams *m_currentParams;
};
}

static Static::Chains s_chains;

const CChainParams &Params() {
    return s_chains.current();
}

CChainParams& Params(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return s_chains.main();
    else if (chain == CBaseChainParams::TESTNET)
        return s_chains.testnet3();
    else if (chain == CBaseChainParams::TESTNET4)
        return s_chains.testnet4();
    else if (chain == CBaseChainParams::SCALENET)
        return s_chains.scalenet();
    else if (chain == CBaseChainParams::REGTEST)
        return s_chains.regtest();
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    s_chains.setCurrent(Params(network));
}

bool ParamsConfigured() {
    return s_chains.isSet();
}
