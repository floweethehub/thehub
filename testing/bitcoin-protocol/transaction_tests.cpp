/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2015 The Bitcoin Core developers
 * Copyright (C) 2016-2021 Tom Zander <tom@flowee.org>
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

#include "transaction_tests.h"

#include "data/tx_invalid.json.h"
#include "data/tx_valid.json.h"
#include "test/test_bitcoin.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "keystore.h"
#include "main.h" // For CheckTransaction
#include "policy/policy.h"
#include "primitives/script.h"
#include "transaction_utils.h"
#include "chainparams.h"
#include <SettingsDefaults.h>
#include <utilstrencodings.h>
#include <clientversion.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/assign/list_of.hpp>

#include <univalue.h>

// In script_tests.cpp
extern UniValue read_json(const std::string& jsondata);

static std::map<std::string, unsigned int> mapFlagNames = boost::assign::map_list_of
    (std::string("NONE"), (unsigned int)SCRIPT_VERIFY_NONE)
    (std::string("P2SH"), (unsigned int)SCRIPT_VERIFY_P2SH)
    (std::string("STRICTENC"), (unsigned int)SCRIPT_VERIFY_STRICTENC)
    (std::string("DERSIG"), (unsigned int)SCRIPT_VERIFY_DERSIG)
    (std::string("LOW_S"), (unsigned int)SCRIPT_VERIFY_LOW_S)
    (std::string("SIGPUSHONLY"), (unsigned int)SCRIPT_VERIFY_SIGPUSHONLY)
    (std::string("MINIMALDATA"), (unsigned int)SCRIPT_VERIFY_MINIMALDATA)
    (std::string("NULLDUMMY"), (unsigned int)SCRIPT_VERIFY_NULLDUMMY)
    (std::string("DISCOURAGE_UPGRADABLE_NOPS"), (unsigned int)SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS)
    (std::string("CLEANSTACK"), (unsigned int)SCRIPT_VERIFY_CLEANSTACK)
    (std::string("NULLFAIL"), (unsigned int)SCRIPT_VERIFY_NULLFAIL)
    (std::string("CHECKLOCKTIMEVERIFY"), (unsigned int)SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY)
    (std::string("CHECKSEQUENCEVERIFY"), (unsigned int)SCRIPT_VERIFY_CHECKSEQUENCEVERIFY)
    (std::string("SIGHASH_FORKID"), (unsigned int)SCRIPT_ENABLE_SIGHASH_FORKID);

unsigned int TransactionTests::parseScriptFlags(const std::string &strFlags)
{
    if (strFlags.empty()) {
        return 0;
    }
    unsigned int flags = 0;
    std::vector<std::string> words;
    boost::algorithm::split(words, strFlags, boost::algorithm::is_any_of(","));

    for (const std::string &word : words) {
        Q_ASSERT(mapFlagNames.count(word)); // if fail; "unknown verification flag");
        flags |= mapFlagNames[word];
    }

    return flags;
}

std::string FormatScriptFlags(unsigned int flags)
{
    if (flags == 0)
        return std::string();
    std::string ret;
    std::map<std::string, unsigned int>::const_iterator it = mapFlagNames.begin();
    while (it != mapFlagNames.end()) {
        if (flags & it->second) {
            ret += it->first + ",";
        }
        it++;
    }
    return ret.substr(0, ret.size() - 1);
}


void TransactionTests::tx_valid()
{
    // Read tests from test/data/tx_valid.json
    // Format is an array of arrays
    // Inner arrays are either [ "comment" ]
    // or [[[prevout hash, prevout index, prevout scriptPubKey], [input 2], ...],"], serializedTransaction, verifyFlags
    // ... where all scripts are stringified scripts.
    //
    // verifyFlags is a comma separated list of script verification flags to apply, or "NONE"
    UniValue tests = read_json(std::string(json_tests::tx_valid, json_tests::tx_valid + sizeof(json_tests::tx_valid)));

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        if (test[0].isArray()) {
            if (test.size() != 3 || !test[1].isStr() || !test[2].isStr() || test[1].get_str().empty() || test[2].get_str().empty()) {
                logCritical() << strTest;
                QFAIL("Bad test");
                continue;
            }

            std::map<COutPoint, CScript> mapprevOutScriptPubKeys;
            UniValue inputs = test[0].get_array();
            bool fValid = true;
            for (unsigned int inpIdx = 0; inpIdx < inputs.size(); inpIdx++) {
                const UniValue& input = inputs[inpIdx];
                if (!input.isArray()) {
                    fValid = false;
                    break;
                }
                UniValue vinput = input.get_array();
                if (vinput.size() != 3) {
                    fValid = false;
                    break;
                }

                mapprevOutScriptPubKeys[COutPoint(uint256S(vinput[0].get_str()), vinput[1].get_int())] = ParseScript(vinput[2].get_str());
            }
            Q_ASSERT(fValid);

            std::string transaction = test[1].get_str();
            CDataStream stream(ParseHex(transaction), SER_NETWORK, PROTOCOL_VERSION);
            CTransaction tx;
            stream >> tx;

            CValidationState state;
            QVERIFY(CheckTransaction(tx, state));
            QVERIFY(state.IsValid());

            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                if (!mapprevOutScriptPubKeys.count(tx.vin[i].prevout))
                    QFAIL("Bad test");

                int64_t amount = 0;
                unsigned int verify_flags = parseScriptFlags(test[2].get_str());
                Script::State state(verify_flags);
                const bool ok = Script::verify(tx.vin[i].scriptSig, mapprevOutScriptPubKeys[tx.vin[i].prevout],
                        TransactionSignatureChecker(&tx, i, amount), state);
                if (!ok)
                    logDebug() << strTest;
                QCOMPARE(state.errorString(), "No error");
                QVERIFY(ok);
            }
        }
    }
}

void TransactionTests::tx_invalid()
{
    // Read tests from test/data/tx_invalid.json
    // Format is an array of arrays
    // Inner arrays are either [ "comment" ]
    // or [[[prevout hash, prevout index, prevout scriptPubKey], [input 2], ...],"], serializedTransaction, verifyFlags
    // ... where all scripts are stringified scripts.
    //
    // verifyFlags is a comma separated list of script verification flags to apply, or "NONE"
    UniValue tests = read_json(std::string(json_tests::tx_invalid, json_tests::tx_invalid + sizeof(json_tests::tx_invalid)));

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        if (test[0].isArray())
        {
            if (test.size() != 3 || !test[1].isStr() || !test[2].isStr())
                QFAIL("Bad test");

            std::map<COutPoint, CScript> mapprevOutScriptPubKeys;
            UniValue inputs = test[0].get_array();
            bool fValid = true;
            for (unsigned int inpIdx = 0; inpIdx < inputs.size(); inpIdx++) {
                const UniValue& input = inputs[inpIdx];
                if (!input.isArray())
                {
                    fValid = false;
                    break;
                }
                UniValue vinput = input.get_array();
                if (vinput.size() != 3)
                {
                    fValid = false;
                    break;
                }

                mapprevOutScriptPubKeys[COutPoint(uint256S(vinput[0].get_str()), vinput[1].get_int())] = ParseScript(vinput[2].get_str());
            }
            Q_ASSERT(fValid);

            std::string transaction = test[1].get_str();
            CDataStream stream(ParseHex(transaction), SER_NETWORK, PROTOCOL_VERSION);
            CTransaction tx;
            stream >> tx;

            CValidationState state;
            fValid = CheckTransaction(tx, state) && state.IsValid();
            Script::State scriptState;

            for (unsigned int i = 0; i < tx.vin.size() && fValid; i++)
            {
                if (!mapprevOutScriptPubKeys.count(tx.vin[i].prevout))
                    QFAIL("Bad test");

                int64_t amount = 0;
                scriptState.flags = parseScriptFlags(test[2].get_str());
                fValid = Script::verify(tx.vin[i].scriptSig, mapprevOutScriptPubKeys[tx.vin[i].prevout],
                                      TransactionSignatureChecker(&tx, i, amount), scriptState);
                if (fValid)
                    QVERIFY(scriptState.error == SCRIPT_ERR_OK);
                else
                    QVERIFY(scriptState.error != SCRIPT_ERR_OK);
            }
            QVERIFY(!fValid);
        }
    }
}

static std::vector<unsigned char> getTestTx()
{
    // Random real transaction (e2769b09e784f32f62ef849763d4f45b98e07ba658647343b915ff832b110436)
    unsigned char ch[] = {0x01, 0x00, 0x00, 0x00, 0x01, 0x6b, 0xff, 0x7f, 0xcd, 0x4f, 0x85, 0x65, 0xef, 0x40, 0x6d, 0xd5, 0xd6, 0x3d, 0x4f, 0xf9, 0x4f, 0x31, 0x8f, 0xe8, 0x20, 0x27, 0xfd, 0x4d, 0xc4, 0x51, 0xb0, 0x44, 0x74, 0x01, 0x9f, 0x74, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x8c, 0x49, 0x30, 0x46, 0x02, 0x21, 0x00, 0xda, 0x0d, 0xc6, 0xae, 0xce, 0xfe, 0x1e, 0x06, 0xef, 0xdf, 0x05, 0x77, 0x37, 0x57, 0xde, 0xb1, 0x68, 0x82, 0x09, 0x30, 0xe3, 0xb0, 0xd0, 0x3f, 0x46, 0xf5, 0xfc, 0xf1, 0x50, 0xbf, 0x99, 0x0c, 0x02, 0x21, 0x00, 0xd2, 0x5b, 0x5c, 0x87, 0x04, 0x00, 0x76, 0xe4, 0xf2, 0x53, 0xf8, 0x26, 0x2e, 0x76, 0x3e, 0x2d, 0xd5, 0x1e, 0x7f, 0xf0, 0xbe, 0x15, 0x77, 0x27, 0xc4, 0xbc, 0x42, 0x80, 0x7f, 0x17, 0xbd, 0x39, 0x01, 0x41, 0x04, 0xe6, 0xc2, 0x6e, 0xf6, 0x7d, 0xc6, 0x10, 0xd2, 0xcd, 0x19, 0x24, 0x84, 0x78, 0x9a, 0x6c, 0xf9, 0xae, 0xa9, 0x93, 0x0b, 0x94, 0x4b, 0x7e, 0x2d, 0xb5, 0x34, 0x2b, 0x9d, 0x9e, 0x5b, 0x9f, 0xf7, 0x9a, 0xff, 0x9a, 0x2e, 0xe1, 0x97, 0x8d, 0xd7, 0xfd, 0x01, 0xdf, 0xc5, 0x22, 0xee, 0x02, 0x28, 0x3d, 0x3b, 0x06, 0xa9, 0xd0, 0x3a, 0xcf, 0x80, 0x96, 0x96, 0x8d, 0x7d, 0xbb, 0x0f, 0x91, 0x78, 0xff, 0xff, 0xff, 0xff, 0x02, 0x8b, 0xa7, 0x94, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x19, 0x76, 0xa9, 0x14, 0xba, 0xde, 0xec, 0xfd, 0xef, 0x05, 0x07, 0x24, 0x7f, 0xc8, 0xf7, 0x42, 0x41, 0xd7, 0x3b, 0xc0, 0x39, 0x97, 0x2d, 0x7b, 0x88, 0xac, 0x40, 0x94, 0xa8, 0x02, 0x00, 0x00, 0x00, 0x00, 0x19, 0x76, 0xa9, 0x14, 0xc1, 0x09, 0x32, 0x48, 0x3f, 0xec, 0x93, 0xed, 0x51, 0xf5, 0xfe, 0x95, 0xe7, 0x25, 0x59, 0xf2, 0xcc, 0x70, 0x43, 0xf9, 0x88, 0xac, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::vector<unsigned char> vch(ch, ch + sizeof(ch) -1);
    return vch;
}

void TransactionTests::basic_transaction_tests()
{

    auto vch = getTestTx();
    CDataStream stream(vch, SER_DISK, CLIENT_VERSION);
    CMutableTransaction tx;
    stream >> tx;
    CValidationState state;
    QVERIFY(CheckTransaction(tx, state) && state.IsValid()); // Simple deserialized transaction should be valid.

    // Check that duplicate txins fail
    tx.vin.push_back(tx.vin[0]);
    QVERIFY(!CheckTransaction(tx, state) || !state.IsValid()); // Transaction with duplicate txins should be invalid.
}

//
// Helper: create two dummy transactions, each with
// two outputs.  The first has 11 and 50 CENT outputs
// paid to a TX_PUBKEY, the second 21 and 22 CENT outputs
// paid to a TX_PUBKEYHASH.
//
static std::vector<CMutableTransaction> SetupDummyInputs(CBasicKeyStore& keystoreRet)
{
    std::vector<CMutableTransaction> dummyTransactions;
    dummyTransactions.resize(2);

    // Add some keys to the keystore:
    CKey key[4];
    for (int i = 0; i < 4; i++)
    {
        key[i].MakeNewKey(i % 2);
        keystoreRet.AddKey(key[i]);
    }

    // Create some dummy input transactions
    dummyTransactions[0].vout.resize(2);
    dummyTransactions[0].vout[0].nValue = 11*CENT;
    dummyTransactions[0].vout[0].scriptPubKey << ToByteVector(key[0].GetPubKey()) << OP_CHECKSIG;
    dummyTransactions[0].vout[1].nValue = 50*CENT;
    dummyTransactions[0].vout[1].scriptPubKey << ToByteVector(key[1].GetPubKey()) << OP_CHECKSIG;

    dummyTransactions[1].vout.resize(2);
    dummyTransactions[1].vout[0].nValue = 21*CENT;
    dummyTransactions[1].vout[0].scriptPubKey = GetScriptForDestination(key[2].GetPubKey().GetID());
    dummyTransactions[1].vout[1].nValue = 22*CENT;
    dummyTransactions[1].vout[1].scriptPubKey = GetScriptForDestination(key[3].GetPubKey().GetID());

    return dummyTransactions;
}

void TransactionTests::test_IsStandard()
{
    LOCK(cs_main);
    CBasicKeyStore keystore;
    std::vector<CMutableTransaction> dummyTransactions = SetupDummyInputs(keystore);

    CMutableTransaction t;
    t.vin.resize(1);
    t.vin[0].prevout.hash = dummyTransactions[0].GetHash();
    t.vin[0].prevout.n = 1;
    t.vin[0].scriptSig << std::vector<unsigned char>(65, 0);
    t.vout.resize(1);
    t.vout[0].nValue = 90*CENT;
    CKey key;
    key.MakeNewKey(true);
    t.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

    std::string reason;
    QVERIFY(IsStandardTx(t, reason));

    // Check dust with default relay fee:
    int64_t nDustThreshold = 182 * minRelayTxFee.GetFeePerK()/1000 * 3;
    QCOMPARE(nDustThreshold, (int64_t) 546);
    // dust:
    t.vout[0].nValue = nDustThreshold - 1;
    QVERIFY(!IsStandardTx(t, reason));
    // not dust:
    t.vout[0].nValue = nDustThreshold;
    QVERIFY(IsStandardTx(t, reason));

    // Check dust with odd relay fee to verify rounding:
    // nDustThreshold = 182 * 1234 / 1000 * 3
    minRelayTxFee = CFeeRate(1234);
    // dust:
    t.vout[0].nValue = 672 - 1;
    QVERIFY(!IsStandardTx(t, reason));
    // not dust:
    t.vout[0].nValue = 672;
    QVERIFY(IsStandardTx(t, reason));
    minRelayTxFee = CFeeRate(Settings::DefaultMinRelayTxFee);

    t.vout[0].scriptPubKey = CScript() << OP_1;
    QVERIFY(!IsStandardTx(t, reason));

    // MAX_OP_RETURN_RELAY-byte TX_NULL_DATA (standard)
    t.vout[0].scriptPubKey = CScript() << OP_RETURN
        << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38"
                    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    QCOMPARE(Settings::MaxOpReturnRelay + 3, t.vout[0].scriptPubKey.size());
    QVERIFY(IsStandardTx(t, reason));

    // MAX_OP_RETURN_RELAY+1-byte TX_NULL_DATA (non-standard)
    t.vout[0].scriptPubKey = CScript() << OP_RETURN
        << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3800"
                    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    QCOMPARE(Settings::MaxOpReturnRelay + 4, t.vout[0].scriptPubKey.size());
    QVERIFY(!IsStandardTx(t, reason));

    // Data payload can be encoded in any way...
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("");
    QVERIFY(IsStandardTx(t, reason));
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("00") << ParseHex("01");
    QVERIFY(IsStandardTx(t, reason));
    // OP_RESERVED *is* considered to be a PUSHDATA type opcode by IsPushOnly()!
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << OP_RESERVED << -1 << 0 << ParseHex("01") << 2 << 3 << 4 << 5 << 6 << 7 << 8 << 9 << 10 << 11 << 12 << 13 << 14 << 15 << 16;
    QVERIFY(IsStandardTx(t, reason));
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << 0 << ParseHex("01") << 2 << ParseHex("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    QVERIFY(IsStandardTx(t, reason));

    // ...so long as it only contains PUSHDATA's
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << OP_RETURN;
    QVERIFY(!IsStandardTx(t, reason));

    // TX_NULL_DATA w/o PUSHDATA
    t.vout.resize(1);
    t.vout[0].scriptPubKey = CScript() << OP_RETURN;
    QVERIFY(IsStandardTx(t, reason));

    // Only one TX_NULL_DATA permitted in all cases
    t.vout.resize(2);
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
    t.vout[1].scriptPubKey = CScript() << OP_RETURN << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
    QVERIFY(!IsStandardTx(t, reason));

    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
    t.vout[1].scriptPubKey = CScript() << OP_RETURN;
    QVERIFY(!IsStandardTx(t, reason));

    t.vout[0].scriptPubKey = CScript() << OP_RETURN;
    t.vout[1].scriptPubKey = CScript() << OP_RETURN;
    QVERIFY(!IsStandardTx(t, reason));
}

void TransactionTests::transactionIter()
{
    auto vch = getTestTx();

    Streaming::BufferPool pl(vch.size());
    memcpy(pl.begin(), vch.data(), vch.size());
    Tx tx(pl.commit(vch.size()));

    auto iter = Tx::Iterator(tx);
    auto type = iter.next();
    QCOMPARE(type, Tx::TxVersion);
    QCOMPARE(iter.intData(), 1);
    type = iter.next();
    QCOMPARE(type, Tx::PrevTxHash);
    QCOMPARE(iter.byteData().size(), 32);
    auto prev = iter.uint256Data();
    QVERIFY(prev == uint256S("0xb4749f017444b051c44dfd2720e88f314ff94f3dd6d56d40ef65854fcd7fff6b"));
    type = iter.next();
    QCOMPARE(type, Tx::PrevTxIndex);
    QCOMPARE(iter.intData(), 0);
    type = iter.next();
    QCOMPARE(type, Tx::TxInScript);
    QCOMPARE(iter.byteData().size(), 140);
    type = iter.next();
    QCOMPARE(type, Tx::Sequence);
    QCOMPARE(iter.uintData(), 0xffffffff);
    QCOMPARE(iter.longData(), (uint64_t) 0xffffffff);
    type = iter.next();
    QCOMPARE(type, Tx::OutputValue);
    QCOMPARE(iter.longData(), (uint64_t) 244623243);
    type = iter.next();
    QCOMPARE(type, Tx::OutputScript);
    QCOMPARE(iter.byteData().size(), 25);
    type = iter.next();
    QCOMPARE(type, Tx::OutputValue);
    QCOMPARE(iter.longData(), (uint64_t) 44602432);
    type = iter.next();
    QCOMPARE(type, Tx::OutputScript);
    QCOMPARE(iter.byteData().size(), 25);
    type = iter.next();
    QCOMPARE(type, Tx::LockTime);
    type = iter.next();
    QCOMPARE(type, Tx::End);
}

void TransactionTests::transactionIter2()
{
    // coinbase-tx
    const CBlock &gb = Params(CBaseChainParams::MAIN).GenesisBlock();
    FastBlock genesisBlock = FastBlock::fromOldBlock(Params(CBaseChainParams::MAIN).GenesisBlock());
    auto iter = Tx::Iterator(genesisBlock);
    auto type = iter.next();
    QCOMPARE(type, Tx::TxVersion);
    QCOMPARE(iter.intData(), 1);
    type = iter.next();
    QCOMPARE(type, Tx::PrevTxHash);
    QCOMPARE(iter.byteData().size(), 32);
    auto prev = iter.uint256Data();
    QVERIFY(prev == uint256S("0x0000000000000000000000000000000000000000000000000000000000000000"));
    type = iter.next();
    QCOMPARE(type, Tx::PrevTxIndex);
    QCOMPARE(iter.intData(), -1);
    type = iter.next();
    QCOMPARE(type, Tx::TxInScript);
    QCOMPARE(iter.byteData().size(), 77);
    type = iter.next();
    QCOMPARE(type, Tx::Sequence);
    QCOMPARE(iter.uintData(), 0xffffffff);
    QCOMPARE(iter.longData(), (uint64_t) 0xffffffff);
    type = iter.next();
    QCOMPARE(type, Tx::OutputValue);
    QCOMPARE(iter.longData(), (uint64_t) 50 * COIN);
    type = iter.next();
    QCOMPARE(type, Tx::OutputScript);
    QCOMPARE(iter.byteData().size(), 67);
    type = iter.next();
    QCOMPARE(type, Tx::LockTime);
    QCOMPARE(iter.intData(), 0);
    type = iter.next();
    QCOMPARE(type, Tx::End);

    Tx tx = iter.prevTx();
    Tx orig = Tx::fromOldTransaction(gb.vtx[0]);
    QCOMPARE(tx.size(), orig.size());
    QVERIFY(tx.createHash() == orig.createHash());

    genesisBlock.findTransactions();
    QCOMPARE(genesisBlock.transactions().size(), (size_t) 1);
    QCOMPARE(genesisBlock.transactions().front().size(), orig.size());
    QVERIFY(genesisBlock.transactions().front().createHash() == orig.createHash());
    QVERIFY(genesisBlock.transactions().front().createHash() == gb.vtx[0].GetHash());
}

