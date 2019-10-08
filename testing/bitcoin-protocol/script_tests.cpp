/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2015 The Bitcoin Core developers
 * Copyright (C) 2016 Tom Zander <tomz@freedommail.ch>
 * Copyright (C) 2018 Jason B. Cox <contact@jasonbcox.com>
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

#include "script_tests.h"
#include "transaction_tests.h"
#include "data/script_invalid.json.h"
#include "data/script_valid.json.h"
#include "rpcserver.h"
#include "transaction_utils.h"

#include "core_io.h"
#include "keystore.h"
#include "primitives/script.h"
#include <script/standard.h>
#include "script/sign.h"
#include <utilstrencodings.h>

// Uncomment if you want to output updated JSON tests.
// #define UPDATE_JSON_TESTS

static const unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;

std::string FormatScriptFlags(unsigned int flags);

UniValue read_json(const std::string& jsondata)
{
    UniValue v;

    if (!v.read(jsondata) || !v.isArray())
    {
        Q_ASSERT(false);
        return UniValue(UniValue::VARR);
    }
    return v.get_array();
}

CMutableTransaction BuildCreditingTransaction(const CScript& scriptPubKey, CAmount amount = 0)
{
    CMutableTransaction txCredit;
    txCredit.nVersion = 1;
    txCredit.nLockTime = 0;
    txCredit.vin.resize(1);
    txCredit.vout.resize(1);
    txCredit.vin[0].prevout.SetNull();
    txCredit.vin[0].scriptSig = CScript() << CScriptNum(0) << CScriptNum(0);
    txCredit.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    txCredit.vout[0].scriptPubKey = scriptPubKey;
    txCredit.vout[0].nValue = amount;

    return txCredit;
}

CMutableTransaction BuildSpendingTransaction(const CScript& scriptSig, const CMutableTransaction& txCredit)
{
    CMutableTransaction txSpend;
    txSpend.nVersion = 1;
    txSpend.nLockTime = 0;
    txSpend.vin.resize(1);
    txSpend.vout.resize(1);
    txSpend.vin[0].prevout.hash = txCredit.GetHash();
    txSpend.vin[0].prevout.n = 0;
    txSpend.vin[0].scriptSig = scriptSig;
    txSpend.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    txSpend.vout[0].scriptPubKey = CScript();
    txSpend.vout[0].nValue = txCredit.vout[0].nValue;

    return txSpend;
}

void DoTest(const CScript& scriptPubKey, const CScript& scriptSig, int flags, bool expect, const QString &message, CAmount nValue)
{
    ScriptError err;
    CMutableTransaction tx = BuildSpendingTransaction(scriptSig, BuildCreditingTransaction(scriptPubKey, nValue));
    CMutableTransaction tx2 = tx;
    QCOMPARE(VerifyScript(scriptSig, scriptPubKey, flags, MutableTransactionSignatureChecker(&tx, 0, nValue), &err), expect);
    if ((err == SCRIPT_ERR_OK) != expect)
        qWarning() << QString::fromStdString(std::string(ScriptErrorString(err)) + ": ")+ message;
    QCOMPARE(err == SCRIPT_ERR_OK, expect);
}

void static NegateSignatureS(std::vector<unsigned char>& vchSig) {
    // Parse the signature.
    std::vector<unsigned char> r, s;
    r = std::vector<unsigned char>(vchSig.begin() + 4, vchSig.begin() + 4 + vchSig[3]);
    s = std::vector<unsigned char>(vchSig.begin() + 6 + vchSig[3], vchSig.begin() + 6 + vchSig[3] + vchSig[5 + vchSig[3]]);

    // Really ugly to implement mod-n negation here, but it would be feature creep to expose such functionality from libsecp256k1.
    static const unsigned char order[33] = {
        0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
        0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
        0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41
    };
    while (s.size() < 33) {
        s.insert(s.begin(), 0x00);
    }
    int carry = 0;
    for (int p = 32; p >= 1; p--) {
        int n = (int)order[p] - s[p] - carry;
        s[p] = (n + 256) & 0xFF;
        carry = (n < 0);
    }
    Q_ASSERT(carry == 0);
    if (s.size() > 1 && s[0] == 0 && s[1] < 0x80) {
        s.erase(s.begin());
    }

    // Reconstruct the signature.
    vchSig.clear();
    vchSig.push_back(0x30);
    vchSig.push_back(4 + r.size() + s.size());
    vchSig.push_back(0x02);
    vchSig.push_back(r.size());
    vchSig.insert(vchSig.end(), r.begin(), r.end());
    vchSig.push_back(0x02);
    vchSig.push_back(s.size());
    vchSig.insert(vchSig.end(), s.begin(), s.end());
}

namespace
{
const unsigned char vchKey0[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
const unsigned char vchKey1[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0};
const unsigned char vchKey2[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0};

struct KeyData
{
    KeyData();

    CKey key0, key0C, key1, key1C, key2, key2C;
    CPubKey pubkey0, pubkey0C, pubkey0H;
    CPubKey pubkey1, pubkey1C;
    CPubKey pubkey2, pubkey2C;
};

KeyData::KeyData()
{
    key0.Set(vchKey0, vchKey0 + 32, false);
    key0C.Set(vchKey0, vchKey0 + 32, true);
    pubkey0 = key0.GetPubKey();
    pubkey0H = key0.GetPubKey();
    pubkey0C = key0C.GetPubKey();
    *const_cast<unsigned char*>(&pubkey0H[0]) = 0x06 | (pubkey0H[64] & 1);

    key1.Set(vchKey1, vchKey1 + 32, false);
    key1C.Set(vchKey1, vchKey1 + 32, true);
    pubkey1 = key1.GetPubKey();
    pubkey1C = key1C.GetPubKey();

    key2.Set(vchKey2, vchKey2 + 32, false);
    key2C.Set(vchKey2, vchKey2 + 32, true);
    pubkey2 = key2.GetPubKey();
    pubkey2C = key2C.GetPubKey();
}

}

void TestBuilder::DoPush()
{
    if (havePush) {
        spendTx.vin[0].scriptSig << push;
        havePush = false;
    }
}

void TestBuilder::DoPush(const std::vector<unsigned char>& data)
{
     DoPush();
     push = data;
     havePush = true;
}

TestBuilder::TestBuilder(const CScript& redeemScript, const QString &comment, int flags_, bool P2SH, CAmount nValue_)
    : scriptPubKey(redeemScript), havePush(false), comment(comment), flags(flags_), nValue(nValue_)
{
    if (P2SH) {
        creditTx = BuildCreditingTransaction(CScript() << OP_HASH160 << ToByteVector(CScriptID(redeemScript)) << OP_EQUAL, nValue);
    } else {
        creditTx = BuildCreditingTransaction(redeemScript, nValue);
    }
    spendTx = BuildSpendingTransaction(CScript(), creditTx);
}

TestBuilder& TestBuilder::Add(const CScript& script)
{
    DoPush();
    spendTx.vin[0].scriptSig += script;
    return *this;
}

TestBuilder& TestBuilder::Num(int num)
{
    DoPush();
    spendTx.vin[0].scriptSig << num;
    return *this;
}

TestBuilder& TestBuilder::Push(const std::string& hex)
{
    DoPush(ParseHex(hex));
    return *this;
}

TestBuilder& TestBuilder::PushSig(const CKey& key, int nHashType, unsigned int lenR, unsigned int lenS, CAmount amount)
{
    uint256 hash = SignatureHash(scriptPubKey, spendTx, 0, amount, nHashType);
    std::vector<unsigned char> vchSig, r, s;
    uint32_t iter = 0;
    do {
        key.Sign(hash, vchSig, iter++);
        if ((lenS == 33) != (vchSig[5 + vchSig[3]] == 33)) {
            NegateSignatureS(vchSig);
        }
        r = std::vector<unsigned char>(vchSig.begin() + 4, vchSig.begin() + 4 + vchSig[3]);
        s = std::vector<unsigned char>(vchSig.begin() + 6 + vchSig[3], vchSig.begin() + 6 + vchSig[3] + vchSig[5 + vchSig[3]]);
    } while (lenR != r.size() || lenS != s.size());
    vchSig.push_back(static_cast<unsigned char>(nHashType));
    DoPush(vchSig);
    return *this;
}

TestBuilder& TestBuilder::Push(const CPubKey& pubkey)
{
    DoPush(std::vector<unsigned char>(pubkey.begin(), pubkey.end()));
    return *this;
}

TestBuilder& TestBuilder::PushRedeem()
{
    DoPush(std::vector<unsigned char>(scriptPubKey.begin(), scriptPubKey.end()));
    return *this;
}

TestBuilder& TestBuilder::EditPush(unsigned int pos, const std::string& hexin, const std::string& hexout)
{
    Q_ASSERT(havePush);
    std::vector<unsigned char> datain = ParseHex(hexin);
    std::vector<unsigned char> dataout = ParseHex(hexout);
    Q_ASSERT(pos + datain.size() <= push.size());
    Q_ASSERT(std::vector<unsigned char>(push.begin() + pos, push.begin() + pos + datain.size()) == datain);
    push.erase(push.begin() + pos, push.begin() + pos + datain.size());
    push.insert(push.begin() + pos, dataout.begin(), dataout.end());
    return *this;
}

TestBuilder& TestBuilder::DamagePush(unsigned int pos)
{
    Q_ASSERT(havePush);
    Q_ASSERT(pos < push.size());
    push[pos] ^= 1;
    return *this;
}

TestBuilder& TestBuilder::Test(bool expect)
{
    TestBuilder copy = *this; // Make a copy so we can rollback the push.
    DoPush();
    DoTest(creditTx.vout[0].scriptPubKey, spendTx.vin[0].scriptSig, flags, expect, comment, nValue);
    *this = copy;
    return *this;
}

UniValue TestBuilder::GetJSON()
{
    DoPush();
    UniValue array(UniValue::VARR);
    if (nValue != 0) {
        UniValue amount(UniValue::VARR);
        amount.push_back(ValueFromAmount(nValue));
        array.push_back(amount);
    }

    array.push_back(TxUtils::FormatScript(spendTx.vin[0].scriptSig));
    array.push_back(TxUtils::FormatScript(creditTx.vout[0].scriptPubKey));
    array.push_back(FormatScriptFlags(flags));
    array.push_back(comment.toStdString());
    return array;
}

QString TestBuilder::GetComment() const
{
    return comment;
}

const CScript& TestBuilder::GetScriptPubKey()
{
    return creditTx.vout[0].scriptPubKey;
}


void TestScript::script_build()
{
    const KeyData keys;

    std::vector<TestBuilder> good;
    std::vector<TestBuilder> bad;

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                               "P2PK", 0
                              ).PushSig(keys.key0));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                              "P2PK, bad sig", 0
                             ).PushSig(keys.key0).DamagePush(10));

    good.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
                               "P2PKH", 0
                              ).PushSig(keys.key1).Push(keys.pubkey1C));
    bad.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey2C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
                              "P2PKH, bad pubkey", 0
                             ).PushSig(keys.key2).Push(keys.pubkey2C).DamagePush(5));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                               "P2PK anyonecanpay", 0
                              ).PushSig(keys.key1, SIGHASH_ALL | SIGHASH_ANYONECANPAY));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                              "P2PK anyonecanpay marked with normal hashtype", 0
                             ).PushSig(keys.key1, SIGHASH_ALL | SIGHASH_ANYONECANPAY).EditPush(70, "81", "01"));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG,
                               "P2SH(P2PK)", SCRIPT_VERIFY_P2SH, true
                              ).PushSig(keys.key0).PushRedeem());
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG,
                              "P2SH(P2PK), bad redeemscript", SCRIPT_VERIFY_P2SH, true
                             ).PushSig(keys.key0).PushRedeem().DamagePush(10));

    good.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
                               "P2SH(P2PKH), bad sig but no VERIFY_P2SH", 0, true
                              ).PushSig(keys.key0).DamagePush(10).PushRedeem());
    bad.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
                              "P2SH(P2PKH), bad sig", SCRIPT_VERIFY_P2SH, true
                             ).PushSig(keys.key0).DamagePush(10).PushRedeem());

    good.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                               "3-of-3", 0
                              ).Num(0).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                              "3-of-3, 2 sigs", 0
                             ).Num(0).PushSig(keys.key0).PushSig(keys.key1).Num(0));

    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                               "P2SH(2-of-3)", SCRIPT_VERIFY_P2SH, true
                              ).Num(0).PushSig(keys.key1).PushSig(keys.key2).PushRedeem());
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                              "P2SH(2-of-3), 1 sig", SCRIPT_VERIFY_P2SH, true
                             ).Num(0).PushSig(keys.key1).Num(0).PushRedeem());

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                               "P2PK with too much R padding but no DERSIG", 0
                              ).PushSig(keys.key1, SIGHASH_ALL, 31, 32).EditPush(1, "43021F", "44022000"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "P2PK with too much R padding", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key1, SIGHASH_ALL, 31, 32).EditPush(1, "43021F", "44022000"));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                               "P2PK with too much S padding but no DERSIG", 0
                              ).PushSig(keys.key1, SIGHASH_ALL).EditPush(1, "44", "45").EditPush(37, "20", "2100"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "P2PK with too much S padding", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key1, SIGHASH_ALL).EditPush(1, "44", "45").EditPush(37, "20", "2100"));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                               "P2PK with too little R padding but no DERSIG", 0
                              ).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "P2PK with too little R padding", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                               "P2PK NOT with bad sig with too much R padding but no DERSIG", 0
                              ).PushSig(keys.key2, SIGHASH_ALL, 31, 32).EditPush(1, "43021F", "44022000").DamagePush(10));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with bad sig with too much R padding", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key2, SIGHASH_ALL, 31, 32).EditPush(1, "43021F", "44022000").DamagePush(10));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with too much R padding but no DERSIG", 0
                             ).PushSig(keys.key2, SIGHASH_ALL, 31, 32).EditPush(1, "43021F", "44022000"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with too much R padding", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key2, SIGHASH_ALL, 31, 32).EditPush(1, "43021F", "44022000"));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                               "BIP66 example 1, without DERSIG", 0
                              ).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "BIP66 example 1, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                              "BIP66 example 2, without DERSIG", 0
                             ).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                              "BIP66 example 2, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "BIP66 example 3, without DERSIG", 0
                             ).Num(0));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "BIP66 example 3, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(0));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                               "BIP66 example 4, without DERSIG", 0
                              ).Num(0));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                               "BIP66 example 4, with DERSIG", SCRIPT_VERIFY_DERSIG
                              ).Num(0));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "BIP66 example 5, without DERSIG", 0
                             ).Num(1));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "BIP66 example 5, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(1));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                               "BIP66 example 6, without DERSIG", 0
                              ).Num(1));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                              "BIP66 example 6, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(1));
    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                               "BIP66 example 7, without DERSIG", 0
                              ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                              "BIP66 example 7, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                              "BIP66 example 8, without DERSIG", 0
                             ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                              "BIP66 example 8, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                              "BIP66 example 9, without DERSIG", 0
                             ).Num(0).Num(0).PushSig(keys.key2, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                              "BIP66 example 9, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(0).Num(0).PushSig(keys.key2, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                               "BIP66 example 10, without DERSIG", 0
                              ).Num(0).Num(0).PushSig(keys.key2, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                              "BIP66 example 10, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(0).Num(0).PushSig(keys.key2, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                              "BIP66 example 11, without DERSIG", 0
                             ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").Num(0));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                              "BIP66 example 11, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").Num(0));
    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                               "BIP66 example 12, without DERSIG", 0
                              ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").Num(0));
    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                               "BIP66 example 12, with DERSIG", SCRIPT_VERIFY_DERSIG
                              ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").Num(0));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                               "P2PK with multi-byte hashtype, without DERSIG", 0
                              ).PushSig(keys.key2, SIGHASH_ALL).EditPush(70, "01", "0101"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                               "P2PK with multi-byte hashtype, with DERSIG", SCRIPT_VERIFY_DERSIG
                              ).PushSig(keys.key2, SIGHASH_ALL).EditPush(70, "01", "0101"));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                               "P2PK with high S but no LOW_S", 0
                              ).PushSig(keys.key2, SIGHASH_ALL, 32, 33));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                              "P2PK with high S", SCRIPT_VERIFY_LOW_S
                             ).PushSig(keys.key2, SIGHASH_ALL, 32, 33));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG,
                               "P2PK with hybrid pubkey but no STRICTENC", 0
                              ).PushSig(keys.key0, SIGHASH_ALL));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG,
                              "P2PK with hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key0, SIGHASH_ALL));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with hybrid pubkey but no STRICTENC", 0
                             ).PushSig(keys.key0, SIGHASH_ALL));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key0, SIGHASH_ALL));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                               "P2PK NOT with invalid hybrid pubkey but no STRICTENC", 0
                              ).PushSig(keys.key0, SIGHASH_ALL).DamagePush(10));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with invalid hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key0, SIGHASH_ALL).DamagePush(10));
    good.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0H) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                               "1-of-2 with the second 1 hybrid pubkey and no STRICTENC", 0
                              ).Num(0).PushSig(keys.key1, SIGHASH_ALL));
    good.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0H) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                               "1-of-2 with the second 1 hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                              ).Num(0).PushSig(keys.key1, SIGHASH_ALL));
    bad.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0H) << OP_2 << OP_CHECKMULTISIG,
                              "1-of-2 with the first 1 hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                             ).Num(0).PushSig(keys.key1, SIGHASH_ALL));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                               "P2PK with undefined hashtype but no STRICTENC", 0
                              ).PushSig(keys.key1, 5));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                              "P2PK with undefined hashtype", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key1, 5));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG << OP_NOT,
                               "P2PK NOT with invalid sig and undefined hashtype but no STRICTENC", 0
                              ).PushSig(keys.key1, 5).DamagePush(10));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with invalid sig and undefined hashtype", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key1, 5).DamagePush(10));

    good.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                               "3-of-3 with nonzero dummy but no NULLDUMMY", 0
                              ).Num(1).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                              "3-of-3 with nonzero dummy", SCRIPT_VERIFY_NULLDUMMY
                             ).Num(1).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2));
    good.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG << OP_NOT,
                               "3-of-3 NOT with invalid sig and nonzero dummy but no NULLDUMMY", 0
                              ).Num(1).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2).DamagePush(10));
    bad.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG << OP_NOT,
                              "3-of-3 NOT with invalid sig with nonzero dummy", SCRIPT_VERIFY_NULLDUMMY
                             ).Num(1).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2).DamagePush(10));

    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                               "2-of-2 with two identical keys and sigs pushed using OP_DUP but no SIGPUSHONLY", 0
                              ).Num(0).PushSig(keys.key1).Add(CScript() << OP_DUP));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                              "2-of-2 with two identical keys and sigs pushed using OP_DUP", SCRIPT_VERIFY_SIGPUSHONLY
                             ).Num(0).PushSig(keys.key1).Add(CScript() << OP_DUP));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                              "P2SH(P2PK) with non-push scriptSig but no SIGPUSHONLY", 0
                             ).PushSig(keys.key2).PushRedeem());
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                              "P2SH(P2PK) with non-push scriptSig", SCRIPT_VERIFY_SIGPUSHONLY
                             ).PushSig(keys.key2).PushRedeem());
    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                               "2-of-2 with two identical keys and sigs pushed", SCRIPT_VERIFY_SIGPUSHONLY
                              ).Num(0).PushSig(keys.key1).PushSig(keys.key1));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                               "P2PK with unnecessary input but no CLEANSTACK", SCRIPT_VERIFY_P2SH
                              ).Num(11).PushSig(keys.key0));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                              "P2PK with unnecessary input", SCRIPT_VERIFY_CLEANSTACK | SCRIPT_VERIFY_P2SH
                             ).Num(11).PushSig(keys.key0));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                               "P2SH with unnecessary input but no CLEANSTACK", SCRIPT_VERIFY_P2SH, true
                              ).Num(11).PushSig(keys.key0).PushRedeem());
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                              "P2SH with unnecessary input", SCRIPT_VERIFY_CLEANSTACK | SCRIPT_VERIFY_P2SH, true
                             ).Num(11).PushSig(keys.key0).PushRedeem());
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                               "P2SH with CLEANSTACK", SCRIPT_VERIFY_CLEANSTACK | SCRIPT_VERIFY_P2SH, true
                              ).PushSig(keys.key0).PushRedeem());

    static const CAmount TEST_AMOUNT = 12345000000000;
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "P2PK FORKID", SCRIPT_ENABLE_SIGHASH_FORKID, false, TEST_AMOUNT)
                    .PushSig(keys.key0, SIGHASH_ALL | SIGHASH_FORKID, 32, 32, TEST_AMOUNT));

    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "P2PK INVALID AMOUNT", SCRIPT_ENABLE_SIGHASH_FORKID, false, TEST_AMOUNT)
                    .PushSig(keys.key0, SIGHASH_ALL | SIGHASH_FORKID, 32, 32, TEST_AMOUNT + 1));

    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "P2PK INVALID FORKID", SCRIPT_VERIFY_STRICTENC, false, TEST_AMOUNT)
                    .PushSig(keys.key0, SIGHASH_ALL | SIGHASH_FORKID, 32, 32, TEST_AMOUNT));

    std::set<std::string> tests_good;
    std::set<std::string> tests_bad;

    {
        UniValue json_good = read_json(std::string(json_tests::script_valid, json_tests::script_valid + sizeof(json_tests::script_valid)));
        UniValue json_bad = read_json(std::string(json_tests::script_invalid, json_tests::script_invalid + sizeof(json_tests::script_invalid)));

        for (unsigned int idx = 0; idx < json_good.size(); idx++) {
            const UniValue& tv = json_good[idx];
            tests_good.insert(tv.get_array().write());
        }
        for (unsigned int idx = 0; idx < json_bad.size(); idx++) {
            const UniValue& tv = json_bad[idx];
            tests_bad.insert(tv.get_array().write());
        }
    }

    std::string strGood;
    std::string strBad;

    for (TestBuilder& test : good) {
        test.Test(true);
        std::string str = test.GetJSON().write();
#ifndef UPDATE_JSON_TESTS
        if (tests_good.count(str) == 0) {
            qWarning() << "Missing auto script_valid test: " << test.GetComment();
            QFAIL("Missing auto script_valid");
        }
#endif
        strGood += str + ",\n";
    }
    for (TestBuilder &test : bad) {
        test.Test(false);
        std::string str = test.GetJSON().write();
#ifndef UPDATE_JSON_TESTS
        if (tests_bad.count(str) == 0) {
            QFAIL("Missing auto script_invalid");
        }
#endif
        strBad += str + ",\n";
    }

#ifdef UPDATE_JSON_TESTS
    FILE* valid = fopen("script_valid.json.gen", "w");
    fputs(strGood.c_str(), valid);
    fclose(valid);
    FILE* invalid = fopen("script_invalid.json.gen", "w");
    fputs(strBad.c_str(), invalid);
    fclose(invalid);
#endif
}

void TestScript::script_valid()
{
    // Read tests from test/data/script_valid.json
    // Format is an array of arrays
    // Inner arrays are [ "scriptSig", "scriptPubKey", "flags" ]
    // ... where scriptSig and scriptPubKey are stringified
    // scripts.
    UniValue tests = read_json(std::string(json_tests::script_valid, json_tests::script_valid + sizeof(json_tests::script_valid)));

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        QString strTest = QString::fromStdString(test.write());
        CAmount nValue = 0;
        unsigned int pos = 0;
        if (test.size() > 0 && test[pos].isArray()) {
            nValue = AmountFromValue(test[pos][0]);
            pos++;
        }
        if (test.size() < 3 + pos) // Allow size > 3; extra stuff ignored (useful for comments)
        {
            if (test.size() != 1) {
                qWarning() << "bad test" << strTest;
                QFAIL("Bad test");
            }
            continue;
        }
        std::string scriptSigString = test[pos++].get_str();
        CScript scriptSig = ParseScript(scriptSigString);
        std::string scriptPubKeyString = test[pos++].get_str();
        CScript scriptPubKey = ParseScript(scriptPubKeyString);
        unsigned int scriptflags = TransactionTests::parseScriptFlags(test[pos++].get_str());

        DoTest(scriptPubKey, scriptSig, scriptflags, true, strTest, nValue);
    }
}

void TestScript::script_invalid()
{
    // Scripts that should evaluate as invalid
    UniValue tests = read_json(std::string(json_tests::script_invalid, json_tests::script_invalid + sizeof(json_tests::script_invalid)));

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        QString strTest = QString::fromStdString(test.write());
        CAmount nValue = 0;
        unsigned int pos = 0;
        if (test.size() > 0 && test[pos].isArray()) {
            nValue = AmountFromValue(test[pos][0]);
            pos++;
        }
        if (test.size() < 3 + pos) // Allow size > 2; extra stuff ignored (useful for comments)
        {
            if (test.size() != 1) {
                qWarning() << "bad test" << strTest;
                QFAIL("Bad test");
            }
            continue;
        }
        std::string scriptSigString = test[pos++].get_str();
        CScript scriptSig = ParseScript(scriptSigString);
        std::string scriptPubKeyString = test[pos++].get_str();
        CScript scriptPubKey = ParseScript(scriptPubKeyString);
        unsigned int scriptflags = TransactionTests::parseScriptFlags(test[pos++].get_str());

        DoTest(scriptPubKey, scriptSig, scriptflags, false, strTest, nValue);
    }
}

void TestScript::script_PushData()
{
    // Check that PUSHDATA1, PUSHDATA2, and PUSHDATA4 create the same value on
    // the stack as the 1-75 opcodes do.
    static const unsigned char direct[] = { 1, 0x5a };
    static const unsigned char pushdata1[] = { OP_PUSHDATA1, 1, 0x5a };
    static const unsigned char pushdata2[] = { OP_PUSHDATA2, 1, 0, 0x5a };
    static const unsigned char pushdata4[] = { OP_PUSHDATA4, 1, 0, 0, 0, 0x5a };

    ScriptError err;
    std::vector<std::vector<unsigned char> > directStack;
    QVERIFY(EvalScript(directStack, CScript(&direct[0], &direct[sizeof(direct)]), SCRIPT_VERIFY_P2SH, BaseSignatureChecker(), &err));
    QCOMPARE(ScriptErrorString(err), "No error");

    std::vector<std::vector<unsigned char> > pushdata1Stack;
    QVERIFY(EvalScript(pushdata1Stack, CScript(&pushdata1[0], &pushdata1[sizeof(pushdata1)]), SCRIPT_VERIFY_P2SH, BaseSignatureChecker(), &err));
    QVERIFY(pushdata1Stack == directStack);
    QCOMPARE(ScriptErrorString(err), "No error");

    std::vector<std::vector<unsigned char> > pushdata2Stack;
    QVERIFY(EvalScript(pushdata2Stack, CScript(&pushdata2[0], &pushdata2[sizeof(pushdata2)]), SCRIPT_VERIFY_P2SH, BaseSignatureChecker(), &err));
    QVERIFY(pushdata2Stack == directStack);
    QCOMPARE(ScriptErrorString(err), "No error");

    std::vector<std::vector<unsigned char> > pushdata4Stack;
    QVERIFY(EvalScript(pushdata4Stack, CScript(&pushdata4[0], &pushdata4[sizeof(pushdata4)]), SCRIPT_VERIFY_P2SH, BaseSignatureChecker(), &err));
    QVERIFY(pushdata4Stack == directStack);
    QCOMPARE(ScriptErrorString(err), "No error");
}

CScript TestScript::sign_multisig(CScript scriptPubKey, std::vector<CKey> keys, CTransaction transaction)
{
    const CAmount amountZero = 0;
    uint256 hash = SignatureHash(scriptPubKey, transaction, 0, amountZero, SIGHASH_ALL);

    CScript result;
    //
    // NOTE: CHECKMULTISIG has an unfortunate bug; it requires
    // one extra item on the stack, before the signatures.
    // Putting OP_0 on the stack is the workaround;
    // fixing the bug would mean splitting the block chain (old
    // clients would not accept new CHECKMULTISIG transactions,
    // and vice-versa)
    //
    result << OP_0;
    for (const CKey &key : keys)
    {
        std::vector<unsigned char> vchSig;
        bool ok = key.Sign(hash, vchSig);
        Q_ASSERT(ok);
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        result << vchSig;
    }
    return result;
}

CScript TestScript::sign_multisig(CScript scriptPubKey, const CKey &key, CTransaction transaction)
{
    std::vector<CKey> keys;
    keys.push_back(key);
    return sign_multisig(scriptPubKey, keys, transaction);
}

void TestScript::script_CHECKMULTISIG12()
{
    ScriptError err;
    CKey key1, key2, key3;
    key1.MakeNewKey(true);
    key2.MakeNewKey(false);
    key3.MakeNewKey(true);

    CScript scriptPubKey12;
    scriptPubKey12 << OP_1 << ToByteVector(key1.GetPubKey()) << ToByteVector(key2.GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CMutableTransaction txFrom12 = BuildCreditingTransaction(scriptPubKey12);
    CMutableTransaction txTo12 = BuildSpendingTransaction(CScript(), txFrom12);

    CScript goodsig1 = sign_multisig(scriptPubKey12, key1, txTo12);
    bool ok = VerifyScript(goodsig1, scriptPubKey12, flags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), &err);
    QCOMPARE(ScriptErrorString(err), "No error");
    QVERIFY(ok);
    txTo12.vout[0].nValue = 2;
    ok = VerifyScript(goodsig1, scriptPubKey12, flags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), &err);
    QCOMPARE(ScriptErrorString(err), ScriptErrorString(SCRIPT_ERR_EVAL_FALSE));
    QVERIFY(!ok);

    CScript goodsig2 = sign_multisig(scriptPubKey12, key2, txTo12);
    QVERIFY(VerifyScript(goodsig2, scriptPubKey12, flags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), &err));
    QCOMPARE(ScriptErrorString(err), "No error");

    CScript badsig1 = sign_multisig(scriptPubKey12, key3, txTo12);
    QVERIFY(!VerifyScript(badsig1, scriptPubKey12, flags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), &err));
    QCOMPARE(ScriptErrorString(err), ScriptErrorString(SCRIPT_ERR_EVAL_FALSE));
}

void TestScript::script_CHECKMULTISIG23()
{
    ScriptError err;
    CKey key1, key2, key3, key4;
    key1.MakeNewKey(true);
    key2.MakeNewKey(false);
    key3.MakeNewKey(true);
    key4.MakeNewKey(false);

    CScript scriptPubKey23;
    scriptPubKey23 << OP_2 << ToByteVector(key1.GetPubKey()) << ToByteVector(key2.GetPubKey()) << ToByteVector(key3.GetPubKey()) << OP_3 << OP_CHECKMULTISIG;

    CMutableTransaction txFrom23 = BuildCreditingTransaction(scriptPubKey23);
    CMutableTransaction txTo23 = BuildSpendingTransaction(CScript(), txFrom23);

    std::vector<CKey> keys;
    keys.push_back(key1); keys.push_back(key2);
    CScript goodsig1 = sign_multisig(scriptPubKey23, keys, txTo23);
    bool ok = VerifyScript(goodsig1, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err);
    QCOMPARE(ScriptErrorString(err), "No error");
    QVERIFY(ok);

    keys.clear();
    keys.push_back(key1); keys.push_back(key3);
    CScript goodsig2 = sign_multisig(scriptPubKey23, keys, txTo23);
    ok = VerifyScript(goodsig2, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err);
    QCOMPARE(ScriptErrorString(err), "No error");
    QVERIFY(ok);

    keys.clear();
    keys.push_back(key2); keys.push_back(key3);
    CScript goodsig3 = sign_multisig(scriptPubKey23, keys, txTo23);
    ok = VerifyScript(goodsig3, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err);
    QCOMPARE(ScriptErrorString(err), "No error");
    QVERIFY(ok);

    keys.clear();
    keys.push_back(key2); keys.push_back(key2); // Can't re-use sig
    CScript badsig1 = sign_multisig(scriptPubKey23, keys, txTo23);
    ok = VerifyScript(badsig1, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err);
    QCOMPARE(ScriptErrorString(err), ScriptErrorString(SCRIPT_ERR_EVAL_FALSE));
    QVERIFY(!ok);

    keys.clear();
    keys.push_back(key2); keys.push_back(key1); // sigs must be in correct order
    CScript badsig2 = sign_multisig(scriptPubKey23, keys, txTo23);
    ok = VerifyScript(badsig2, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err);
    QCOMPARE(ScriptErrorString(err), ScriptErrorString(SCRIPT_ERR_EVAL_FALSE));
    QVERIFY(!ok);

    keys.clear();
    keys.push_back(key3); keys.push_back(key2); // sigs must be in correct order
    CScript badsig3 = sign_multisig(scriptPubKey23, keys, txTo23);
    ok = VerifyScript(badsig3, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err);
    QCOMPARE(ScriptErrorString(err), ScriptErrorString(SCRIPT_ERR_EVAL_FALSE));
    QVERIFY(!ok);

    keys.clear();
    keys.push_back(key4); keys.push_back(key2); // sigs must match pubkeys
    CScript badsig4 = sign_multisig(scriptPubKey23, keys, txTo23);
    ok = VerifyScript(badsig4, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err);
    QCOMPARE(ScriptErrorString(err), ScriptErrorString(SCRIPT_ERR_EVAL_FALSE));
    QVERIFY(!ok);

    keys.clear();
    keys.push_back(key1); keys.push_back(key4); // sigs must match pubkeys
    CScript badsig5 = sign_multisig(scriptPubKey23, keys, txTo23);
    ok = VerifyScript(badsig5, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err);
    QCOMPARE(ScriptErrorString(err), ScriptErrorString(SCRIPT_ERR_EVAL_FALSE));
    QVERIFY(!ok);

    keys.clear(); // Must have signatures
    CScript badsig6 = sign_multisig(scriptPubKey23, keys, txTo23);
    ok = VerifyScript(badsig6, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err);
    QCOMPARE(ScriptErrorString(err), ScriptErrorString(SCRIPT_ERR_INVALID_STACK_OPERATION));
    QVERIFY(!ok);
}

void TestScript::script_combineSigs()
{
    // Test the CombineSignatures function
    CAmount amount = 0;
    CBasicKeyStore keystore;
    std::vector<CKey> keys;
    std::vector<CPubKey> pubkeys;
    for (int i = 0; i < 3; i++)
    {
        CKey key;
        key.MakeNewKey(i%2 == 1);
        keys.push_back(key);
        pubkeys.push_back(key.GetPubKey());
        keystore.AddKey(key);
    }

    CMutableTransaction txFrom = BuildCreditingTransaction(GetScriptForDestination(keys[0].GetPubKey().GetID()));
    CMutableTransaction txTo = BuildSpendingTransaction(CScript(), txFrom);
    CScript& scriptPubKey = txFrom.vout[0].scriptPubKey;
    CScript& scriptSig = txTo.vin[0].scriptSig;

    CScript empty;
    CScript combined = CombineSignatures(scriptPubKey, txTo, 0, amount, empty, empty);
    QVERIFY(combined.empty());

    // Single signature case:
    SignSignature(keystore, txFrom, txTo, 0); // changes scriptSig
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, scriptSig, empty);
    QVERIFY(combined == scriptSig);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, empty, scriptSig);
    QVERIFY(combined == scriptSig);
    CScript scriptSigCopy = scriptSig;
    // Signing again will give a different, valid signature:
    SignSignature(keystore, txFrom, txTo, 0);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, scriptSigCopy, scriptSig);
    QVERIFY(combined == scriptSigCopy || combined == scriptSig);

    // P2SH, single-signature case:
    CScript pkSingle; pkSingle << ToByteVector(keys[0].GetPubKey()) << OP_CHECKSIG;
    keystore.AddCScript(pkSingle);
    scriptPubKey = GetScriptForDestination(CScriptID(pkSingle));
    SignSignature(keystore, txFrom, txTo, 0);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, scriptSig, empty);
    QVERIFY(combined == scriptSig);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, empty, scriptSig);
    QVERIFY(combined == scriptSig);
    scriptSigCopy = scriptSig;
    SignSignature(keystore, txFrom, txTo, 0);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, scriptSigCopy, scriptSig);
    QVERIFY(combined == scriptSigCopy || combined == scriptSig);
    // dummy scriptSigCopy with placeholder, should always choose non-placeholder:
    scriptSigCopy = CScript() << OP_0 << std::vector<unsigned char>(pkSingle.begin(), pkSingle.end());
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, scriptSigCopy, scriptSig);
    QVERIFY(combined == scriptSig);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, scriptSig, scriptSigCopy);
    QVERIFY(combined == scriptSig);

    // Hardest case:  Multisig 2-of-3
    scriptPubKey = GetScriptForMultisig(2, pubkeys);
    keystore.AddCScript(scriptPubKey);
    SignSignature(keystore, txFrom, txTo, 0);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, scriptSig, empty);
    QVERIFY(combined == scriptSig);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, empty, scriptSig);
    QVERIFY(combined == scriptSig);

    // A couple of partially-signed versions:
    std::vector<unsigned char> sig1;
    uint256 hash1 = SignatureHash(scriptPubKey, txTo, 0, amount, SIGHASH_ALL);
    QVERIFY(keys[0].Sign(hash1, sig1));
    sig1.push_back(SIGHASH_ALL);
    std::vector<unsigned char> sig2;
    uint256 hash2 = SignatureHash(scriptPubKey, txTo, 0, amount, SIGHASH_NONE);
    QVERIFY(keys[1].Sign(hash2, sig2));
    sig2.push_back(SIGHASH_NONE);
    std::vector<unsigned char> sig3;
    uint256 hash3 = SignatureHash(scriptPubKey, txTo, 0, amount, SIGHASH_SINGLE);
    QVERIFY(keys[2].Sign(hash3, sig3));
    sig3.push_back(SIGHASH_SINGLE);

    // Not fussy about order (or even existence) of placeholders or signatures:
    CScript partial1a = CScript() << OP_0 << sig1 << OP_0;
    CScript partial1b = CScript() << OP_0 << OP_0 << sig1;
    CScript partial2a = CScript() << OP_0 << sig2;
    CScript partial2b = CScript() << sig2 << OP_0;
    CScript partial3a = CScript() << sig3;
    CScript partial3b = CScript() << OP_0 << OP_0 << sig3;
    CScript partial3c = CScript() << OP_0 << sig3 << OP_0;
    CScript complete12 = CScript() << OP_0 << sig1 << sig2;
    CScript complete13 = CScript() << OP_0 << sig1 << sig3;
    CScript complete23 = CScript() << OP_0 << sig2 << sig3;

    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, partial1a, partial1b);
    QVERIFY(combined == partial1a);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, partial1a, partial2a);
    QVERIFY(combined == complete12);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, partial2a, partial1a);
    QVERIFY(combined == complete12);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, partial1b, partial2b);
    QVERIFY(combined == complete12);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, partial3b, partial1b);
    QVERIFY(combined == complete13);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, partial2a, partial3a);
    QVERIFY(combined == complete23);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, partial3b, partial2b);
    QVERIFY(combined == complete23);
    combined = CombineSignatures(scriptPubKey, txTo, 0, amount, partial3b, partial3a);
    QVERIFY(combined == partial3c);
}

void TestScript::script_standard_push()
{
    ScriptError err;
    for (int i=0; i<67000; i++) {
        CScript script;
        script << i;
        QVERIFY(script.IsPushOnly());
        QVERIFY(VerifyScript(script, CScript() << OP_1, SCRIPT_VERIFY_MINIMALDATA, BaseSignatureChecker(), &err));
        QCOMPARE(ScriptErrorString(err), "No error");
    }

    for (unsigned int i=0; i<=MAX_SCRIPT_ELEMENT_SIZE; i++) {
        std::vector<unsigned char> data(i, '\111');
        CScript script;
        script << data;
        QVERIFY(script.IsPushOnly());
        QVERIFY(VerifyScript(script, CScript() << OP_1, SCRIPT_VERIFY_MINIMALDATA, BaseSignatureChecker(), &err));
        QCOMPARE(ScriptErrorString(err), "No error");
    }
}

void TestScript::script_IsPushOnly_on_invalid_scripts()
{
    // IsPushOnly returns false when given a script containing only pushes that
    // are invalid due to truncation. IsPushOnly() is consensus critical
    // because P2SH evaluation uses it, although this specific behavior should
    // not be consensus critical as the P2SH evaluation would fail first due to
    // the invalid push. Still, it doesn't hurt to test it explicitly.
    static const unsigned char direct[] = { 1 };
    QVERIFY(!CScript(direct, direct+sizeof(direct)).IsPushOnly());
}

void TestScript::script_GetScriptAsm()
{
    const std::string OpCheckLocktimeVerify("OP_CHECKLOCKTIMEVERIFY");
    QCOMPARE(OpCheckLocktimeVerify, ScriptToAsmStr(CScript() << OP_NOP2, true));
    QCOMPARE(OpCheckLocktimeVerify, ScriptToAsmStr(CScript() << OP_CHECKLOCKTIMEVERIFY, true));
    QCOMPARE(OpCheckLocktimeVerify, ScriptToAsmStr(CScript() << OP_NOP2));
    QCOMPARE(OpCheckLocktimeVerify, ScriptToAsmStr(CScript() << OP_CHECKLOCKTIMEVERIFY));

    std::string derSig("304502207fa7a6d1e0ee81132a269ad84e68d695483745cde8b541e3bf630749894e342a022100c1f7ab20e13e22fb95281a870f3dcf38d782e53023ee313d741ad0cfbc0c5090");
    std::string pubKey("03b0da749730dc9b4b1f4a14d6902877a92541f5368778853d9c4a0cb7802dcfb2");
    std::vector<unsigned char> vchPubKey = ToByteVector(ParseHex(pubKey));

    QCOMPARE(derSig + "00 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "00")) << vchPubKey, true));
    QCOMPARE(derSig + "80 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "80")) << vchPubKey, true));
    QCOMPARE(derSig + "[ALL] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "01")) << vchPubKey, true));
    QCOMPARE(derSig + "[NONE] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "02")) << vchPubKey, true));
    QCOMPARE(derSig + "[SINGLE] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "03")) << vchPubKey, true));
    QCOMPARE(derSig + "[ALL|ANYONECANPAY] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "81")) << vchPubKey, true));
    QCOMPARE(derSig + "[NONE|ANYONECANPAY] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "82")) << vchPubKey, true));
    QCOMPARE(derSig + "[SINGLE|ANYONECANPAY] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "83")) << vchPubKey, true));

    QCOMPARE(derSig + "00 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "00")) << vchPubKey));
    QCOMPARE(derSig + "80 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "80")) << vchPubKey));
    QCOMPARE(derSig + "01 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "01")) << vchPubKey));
    QCOMPARE(derSig + "02 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "02")) << vchPubKey));
    QCOMPARE(derSig + "03 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "03")) << vchPubKey));
    QCOMPARE(derSig + "81 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "81")) << vchPubKey));
    QCOMPARE(derSig + "82 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "82")) << vchPubKey));
    QCOMPARE(derSig + "83 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "83")) << vchPubKey));

    QCOMPARE(derSig + "[NONE|FORKID] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "42")) << vchPubKey, true));
    QCOMPARE(derSig + "[NONE|ANYONECANPAY|FORKID] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "c2")) << vchPubKey, true));
    QCOMPARE(derSig + "[SINGLE] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "03")) << vchPubKey, true));
    QCOMPARE(derSig + "[SINGLE|ANYONECANPAY] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "83")) << vchPubKey, true));
    QCOMPARE(derSig + "[SINGLE|FORKID] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "43")) << vchPubKey, true));
    QCOMPARE(derSig + "[SINGLE|ANYONECANPAY|FORKID] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "c3")) << vchPubKey, true));
}

void TestScript::minimize_big_endian_test()
{
    // Empty array case
    QVERIFY(MinimalizeBigEndianArray(std::vector<uint8_t>()) == std::vector<uint8_t>());

    // Zero arrays of various lengths
    std::vector<uint8_t> zeroArray({0x00});
    std::vector<uint8_t> negZeroArray({0x80});
    for (int i = 0; i < 16; i++) {
        if (i > 0) {
            zeroArray.push_back(0x00);
            negZeroArray.push_back(0x00);
        }

        QVERIFY(MinimalizeBigEndianArray(zeroArray) == std::vector<uint8_t>());

        // -0 should always evaluate to 0x00
        QVERIFY(MinimalizeBigEndianArray(negZeroArray) == std::vector<uint8_t>());
    }

    // Shouldn't minimalize this array to a negative number
    std::vector<uint8_t> notNegArray({{0x00, 0x80}});
    std::vector<uint8_t> notNegArrayPadded({{0x00, 0x80}});
    for (int i = 0; i < 16; i++) {
        notNegArray.push_back(i);
        notNegArrayPadded.insert(notNegArrayPadded.begin(), 0x00);
        QVERIFY(MinimalizeBigEndianArray(notNegArray) == notNegArray);
        QVERIFY(MinimalizeBigEndianArray(notNegArrayPadded) == std::vector<uint8_t>({{0x00, 0x80}}));
    }

    // Shouldn't minimalize these arrays at all
    std::vector<uint8_t> noMinArray;
    for (int i = 1; i < 0x80; i++) {
        noMinArray.push_back(i);
        QVERIFY(MinimalizeBigEndianArray(noMinArray) == noMinArray);
    }
}

void TestScript::minimalPush()
{
    // Ensure that CheckMinimalPush always return true for non "pushing" opcodes
    std::vector<uint8_t> dummy{};
    for (const auto opcode : {OP_1NEGATE, OP_1, OP_2, OP_3, OP_4, OP_5, OP_6, OP_7, OP_8, OP_9, OP_10, OP_11, OP_12,
             OP_13, OP_14, OP_15, OP_16})
    {
        QCOMPARE(CheckMinimalPush(dummy, opcode), true);
    }

    // Ensure that CheckMinimalPush return false in case we are try use a push opcodes operator whereas
    // we should have use OP_0 instead (i.e. data array is empty array)
    for (const auto opcode_b : {OP_PUSHDATA1, OP_PUSHDATA2, OP_PUSHDATA4})
    {
        QCOMPARE(CheckMinimalPush(dummy, opcode_b), false);
    }

    // If data.size() is equal to 1 we should have used OP_1 .. OP_16.
    dummy = {0};
    QCOMPARE(CheckMinimalPush(dummy, OP_PUSHDATA4), false);

    // Initialize the vector s to that its size is between 2 and 75
    for (int i = 0; i <= 10; i++)
    {
        dummy.push_back(1);
    }
    // In this case we should a direct push (opcode indicating number of bytes  pushed + those bytes)
    QCOMPARE(CheckMinimalPush(dummy, OP_PUSHDATA4), false);

    // extend it to have the length between 76 and 255
    for (int i = 11; i < 240; i++)
    {
        dummy.push_back(1);
    }
    // in this case we must have used OP_PUSHDATA1
    QCOMPARE(CheckMinimalPush(dummy, OP_PUSHDATA4), false);
    QCOMPARE(CheckMinimalPush(dummy, OP_PUSHDATA1), true);

    // extend it to have the length between 256 and 65535
    for (int i = 241; i < 300; i++)
    {
        dummy.push_back(1);
    }
    // in this case we must have used OP_PUSHDATA2
    QCOMPARE(CheckMinimalPush(dummy, OP_PUSHDATA4), false);
    QCOMPARE(CheckMinimalPush(dummy, OP_PUSHDATA2), true);
}

