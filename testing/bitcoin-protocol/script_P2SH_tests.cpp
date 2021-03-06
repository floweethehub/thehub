/*
 * This file is part of the Flowee project
 * Copyright (C) 2012-2015 The Bitcoin Core developers
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

#include <common/MutableTransactionSignatureChecker.h>

#include "script_P2SH_tests.h"
#include "keystore.h"
#include "main.h"
#include "policy/policy.h"
#include "script/sign.h"
#include <script/interpreter.h>

#include <utxo/UnspentOutputDatabase.h>

#ifdef ENABLE_WALLET
#include "wallet/wallet_ismine.h"
#endif

// Helpers:
static std::vector<unsigned char>
Serialize(const CScript& s)
{
    std::vector<unsigned char> sSerialized(s.begin(), s.end());
    return sSerialized;
}

static bool
Verify(const CScript& scriptSig, const CScript& scriptPubKey, bool fStrict, ScriptError& err)
{
    // Create dummy to/from transactions:
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;

    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vin[0].scriptSig = scriptSig;
    txTo.vout[0].nValue = 1;

    Script::State state(fStrict ? SCRIPT_VERIFY_P2SH : SCRIPT_VERIFY_NONE);
    bool ok = Script::verify(scriptSig, scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, txFrom.vout[0].nValue), state);
    err = state.error;
    return ok;
}


void TestPaymentToScriptHash::sign()
{
    LOCK(cs_main);
    // Pay-to-script-hash looks like this:
    // scriptSig:    <sig> <sig...> <serialized_script>
    // scriptPubKey: HASH160 <hash> EQUAL

    // Test SignSignature() (and therefore the version of Solver() that signs transactions)
    CBasicKeyStore keystore;
    CKey key[4];
    for (int i = 0; i < 4; i++)
    {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
    }

    // 8 Scripts: checking all combinations of
    // different keys, straight/P2SH, pubkey/pubkeyhash
    CScript standardScripts[4];
    standardScripts[0] << ToByteVector(key[0].GetPubKey()) << OP_CHECKSIG;
    standardScripts[1] = GetScriptForDestination(key[1].GetPubKey().GetID());
    standardScripts[2] << ToByteVector(key[1].GetPubKey()) << OP_CHECKSIG;
    standardScripts[3] = GetScriptForDestination(key[2].GetPubKey().GetID());
    CScript evalScripts[4];
    for (int i = 0; i < 4; i++)
    {
        keystore.AddCScript(standardScripts[i]);
        evalScripts[i] = GetScriptForDestination(CScriptID(standardScripts[i]));
    }

    CMutableTransaction txFrom;  // Funding transaction:
    std::string reason;
    txFrom.vout.resize(8);
    for (int i = 0; i < 4; i++)
    {
        txFrom.vout[i].scriptPubKey = evalScripts[i];
        txFrom.vout[i].nValue = COIN;
        txFrom.vout[i+4].scriptPubKey = standardScripts[i];
        txFrom.vout[i+4].nValue = COIN;
    }
    QVERIFY(IsStandardTx(txFrom, reason));

    CMutableTransaction txTo[8]; // Spending transactions
    for (int i = 0; i < 8; i++)
    {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1;
#ifdef ENABLE_WALLET
        QVERIFY(IsMine(keystore, txFrom.vout[i].scriptPubKey));
#endif
    }
    for (int i = 0; i < 8; i++)
    {
        QVERIFY(SignSignature(keystore, txFrom, txTo[i], 0));
    }
    // All of the above should be OK, and the txTos have valid signatures
    // Check to make sure signature verification fails if we use the wrong ScriptSig:
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
        {
            CScript sigSave = txTo[i].vin[0].scriptSig;
            txTo[i].vin[0].scriptSig = txTo[j].vin[0].scriptSig;
            const CTransaction &txToIn = txTo[i];
            Script::State state(SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC);

            bool sigOK = Script::verify(txToIn.vin[0].scriptSig, txFrom.vout[txToIn.vin[0].prevout.n].scriptPubKey,
                    TransactionSignatureChecker(&txToIn, 0, txFrom.vout[txToIn.vin[0].prevout.n].nValue), state);
            if (i == j)
                QVERIFY(sigOK);
            else
                QVERIFY(!sigOK);
            txTo[i].vin[0].scriptSig = sigSave;
        }
}

void TestPaymentToScriptHash::norecurse()
{
    ScriptError err;
    // Make sure only the outer pay-to-script-hash does the
    // extra-validation thing:
    CScript invalidAsScript;
    invalidAsScript << INVALIDOPCODE << INVALIDOPCODE;

    CScript p2sh = GetScriptForDestination(CScriptID(invalidAsScript));

    CScript scriptSig;
    scriptSig << Serialize(invalidAsScript);

    // Should not verify, because it will try to execute OP_INVALIDOPCODE
    QVERIFY(!Verify(scriptSig, p2sh, true, err));
    QCOMPARE(err, SCRIPT_ERR_BAD_OPCODE);

    // Try to recur, and verification should succeed because
    // the inner HASH160 <> EQUAL should only check the hash:
    CScript p2sh2 = GetScriptForDestination(CScriptID(p2sh));
    CScript scriptSig2;
    scriptSig2 << Serialize(invalidAsScript) << Serialize(p2sh);

    QVERIFY(Verify(scriptSig2, p2sh2, true, err));
    QCOMPARE(err, SCRIPT_ERR_OK);
}

void TestPaymentToScriptHash::set()
{
    LOCK(cs_main);
    // Test the CScript::Set* methods
    CBasicKeyStore keystore;
    CKey key[4];
    std::vector<CPubKey> keys;
    for (int i = 0; i < 4; i++)
    {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
        keys.push_back(key[i].GetPubKey());
    }

    CScript inner[4];
    inner[0] = GetScriptForDestination(key[0].GetPubKey().GetID());
    inner[1] = GetScriptForMultisig(2, std::vector<CPubKey>(keys.begin(), keys.begin()+2));
    inner[2] = GetScriptForMultisig(1, std::vector<CPubKey>(keys.begin(), keys.begin()+2));
    inner[3] = GetScriptForMultisig(2, std::vector<CPubKey>(keys.begin(), keys.begin()+3));

    CScript outer[4];
    for (int i = 0; i < 4; i++)
    {
        outer[i] = GetScriptForDestination(CScriptID(inner[i]));
        keystore.AddCScript(inner[i]);
    }

    CMutableTransaction txFrom;  // Funding transaction:
    std::string reason;
    txFrom.vout.resize(4);
    for (int i = 0; i < 4; i++)
    {
        txFrom.vout[i].scriptPubKey = outer[i];
        txFrom.vout[i].nValue = CENT;
    }
    QVERIFY(IsStandardTx(txFrom, reason));

    CMutableTransaction txTo[4]; // Spending transactions
    for (int i = 0; i < 4; i++)
    {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1*CENT;
        txTo[i].vout[0].scriptPubKey = inner[i];
#ifdef ENABLE_WALLET
        QVERIFY(IsMine(keystore, txFrom.vout[i].scriptPubKey));
#endif
    }
    for (int i = 0; i < 4; i++)
    {
        QVERIFY(SignSignature(keystore, txFrom, txTo[i], 0));
        QVERIFY(IsStandardTx(txTo[i], reason));
    }
}

void TestPaymentToScriptHash::is()
{
    // Test CScript::IsPayToScriptHash()
    uint160 dummy;
    CScript p2sh;
    p2sh << OP_HASH160 << ToByteVector(dummy) << OP_EQUAL;
    QVERIFY(p2sh.IsPayToScriptHash());

    // Not considered pay-to-script-hash if using one of the OP_PUSHDATA opcodes:
    static const unsigned char direct[] =    { OP_HASH160, 20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUAL };
    QVERIFY(CScript(direct, direct+sizeof(direct)).IsPayToScriptHash());
    static const unsigned char pushdata1[] = { OP_HASH160, OP_PUSHDATA1, 20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUAL };
    QVERIFY(!CScript(pushdata1, pushdata1+sizeof(pushdata1)).IsPayToScriptHash());
    static const unsigned char pushdata2[] = { OP_HASH160, OP_PUSHDATA2, 20,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUAL };
    QVERIFY(!CScript(pushdata2, pushdata2+sizeof(pushdata2)).IsPayToScriptHash());
    static const unsigned char pushdata4[] = { OP_HASH160, OP_PUSHDATA4, 20,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUAL };
    QVERIFY(!CScript(pushdata4, pushdata4+sizeof(pushdata4)).IsPayToScriptHash());

    CScript not_p2sh;
    QVERIFY(!not_p2sh.IsPayToScriptHash());

    not_p2sh.clear(); not_p2sh << OP_HASH160 << ToByteVector(dummy) << ToByteVector(dummy) << OP_EQUAL;
    QVERIFY(!not_p2sh.IsPayToScriptHash());

    not_p2sh.clear(); not_p2sh << OP_NOP << ToByteVector(dummy) << OP_EQUAL;
    QVERIFY(!not_p2sh.IsPayToScriptHash());

    not_p2sh.clear(); not_p2sh << OP_HASH160 << ToByteVector(dummy) << OP_CHECKSIG;
    QVERIFY(!not_p2sh.IsPayToScriptHash());
}

void TestPaymentToScriptHash::switchover()
{
    // Test switch over code
    CScript notValid;
    ScriptError err;
    notValid << OP_11 << OP_12 << OP_EQUALVERIFY;
    CScript scriptSig;
    scriptSig << Serialize(notValid);

    CScript fund = GetScriptForDestination(CScriptID(notValid));


    // Validation should succeed under old rules (hash is correct):
    QVERIFY(Verify(scriptSig, fund, false, err));
    QCOMPARE(err, SCRIPT_ERR_OK);
    // Fail under new:
    QVERIFY(!Verify(scriptSig, fund, true, err));
    QCOMPARE(err, SCRIPT_ERR_EQUALVERIFY);
}

void TestPaymentToScriptHash::AreInputsStandard()
{
    LOCK(cs_main);
    CBasicKeyStore keystore;
    CKey key[6];
    std::vector<CPubKey> keys;
    for (int i = 0; i < 6; i++)
    {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
    }
    for (int i = 0; i < 3; i++)
        keys.push_back(key[i].GetPubKey());

    CMutableTransaction txFrom;
    txFrom.vout.resize(7);

    // First three are standard:
    CScript pay1 = GetScriptForDestination(key[0].GetPubKey().GetID());
    keystore.AddCScript(pay1);
    CScript pay1of3 = GetScriptForMultisig(1, keys);

    txFrom.vout[0].scriptPubKey = GetScriptForDestination(CScriptID(pay1)); // P2SH (OP_CHECKSIG)
    txFrom.vout[0].nValue = 1000;
    txFrom.vout[1].scriptPubKey = pay1; // ordinary OP_CHECKSIG
    txFrom.vout[1].nValue = 2000;
    txFrom.vout[2].scriptPubKey = pay1of3; // ordinary OP_CHECKMULTISIG
    txFrom.vout[2].nValue = 3000;

    // vout[3] is complicated 1-of-3 AND 2-of-3
    // ... that is OK if wrapped in P2SH:
    CScript oneAndTwo;
    oneAndTwo << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey());
    oneAndTwo << OP_3 << OP_CHECKMULTISIGVERIFY;
    oneAndTwo << OP_2 << ToByteVector(key[3].GetPubKey()) << ToByteVector(key[4].GetPubKey()) << ToByteVector(key[5].GetPubKey());
    oneAndTwo << OP_3 << OP_CHECKMULTISIG;
    keystore.AddCScript(oneAndTwo);
    txFrom.vout[3].scriptPubKey = GetScriptForDestination(CScriptID(oneAndTwo));
    txFrom.vout[3].nValue = 4000;

    // vout[4] is max sigchecks: Non-standard because its too long
    CScript fifteenSigops; fifteenSigops << OP_1;
    for (unsigned i = 0; i < Policy::MAX_SIGCHEKCS_PER_TX; i++)
        fifteenSigops << ToByteVector(key[i%3].GetPubKey());
    fifteenSigops << OP_15 << OP_CHECKMULTISIG;
    keystore.AddCScript(fifteenSigops);
    txFrom.vout[4].scriptPubKey = GetScriptForDestination(CScriptID(fifteenSigops));
    txFrom.vout[4].nValue = 5000;

    // vout[5/6] are non-standard because they exceed MAX_P2SH_SIGOPS
    CScript sixteenSigops; sixteenSigops << OP_16 << OP_CHECKMULTISIG;
    keystore.AddCScript(sixteenSigops);
    txFrom.vout[5].scriptPubKey = GetScriptForDestination(CScriptID(fifteenSigops));
    txFrom.vout[5].nValue = 5000;
    CScript twentySigops; twentySigops << OP_CHECKMULTISIG;
    keystore.AddCScript(twentySigops);
    txFrom.vout[6].scriptPubKey = GetScriptForDestination(CScriptID(twentySigops));
    txFrom.vout[6].nValue = 6000;

    CMutableTransaction txTo;
    txTo.vout.resize(1);
    txTo.vout[0].scriptPubKey = GetScriptForDestination(key[1].GetPubKey().GetID());

    txTo.vin.resize(5);
    for (int i = 0; i < 5; i++)
    {
        txTo.vin[i].prevout.n = i;
        txTo.vin[i].prevout.hash = txFrom.GetHash();
    }
    QVERIFY(SignSignature(keystore, txFrom, txTo, 0));
    QVERIFY(SignSignature(keystore, txFrom, txTo, 1));
    QVERIFY(SignSignature(keystore, txFrom, txTo, 2));
    // SignSignature doesn't know how to sign these. We're
    // not testing validating signatures, so just create
    // dummy signatures that DO include the correct P2SH scripts:
    txTo.vin[3].scriptSig << OP_11 << OP_11 << std::vector<unsigned char>(oneAndTwo.begin(), oneAndTwo.end());
    txTo.vin[4].scriptSig << std::vector<unsigned char>(fifteenSigops.begin(), fifteenSigops.end());

    for (size_t i = 0; i < txTo.vin.size(); ++i) {
        const auto in = txTo.vin.at(i);
        const auto prevOut = txFrom.vout.at(i);
        const bool ok = Policy::isInputStandard(prevOut.scriptPubKey, in.scriptSig);
        QCOMPARE(ok, i < 4);
    }
}
