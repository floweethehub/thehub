/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2013 The Bitcoin Core developers
 * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
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

#include "multisig_tests.h"
#include <common/MutableTransactionSignatureChecker.h>

#include <primitives/transaction.h>
#include "keystore.h"
#include "policy/policy.h"
#include <script/sign.h>

#ifdef ENABLE_WALLET
# include "wallet/wallet_ismine.h"
#endif

#include <vector>

typedef std::vector<unsigned char> valtype;


CScript sign_multisig(CScript scriptPubKey, std::vector<CKey> keys, const CTransaction &transaction, int whichIn)
{
    uint256 hash = SignatureHash(scriptPubKey, transaction, whichIn, 0, SIGHASH_ALL);

    CScript result;
    result << OP_0; // CHECKMULTISIG bug workaround
    for (const CKey &key : keys) {
        std::vector<unsigned char> vchSig;
        const bool rc = key.Sign(hash, vchSig);
        assert(rc);
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        result << vchSig;
    }
    return result;
}

void MultiSigTests::multisig_verify()
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;

    CKey key[4];
    CAmount amount = 0;
    for (int i = 0; i < 4; i++)
        key[i].MakeNewKey(true);

    CScript a_and_b;
    a_and_b << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript a_or_b;
    a_or_b << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript escrow;
    escrow << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;

    CMutableTransaction txFrom;  // Funding transaction
    txFrom.vout.resize(3);
    txFrom.vout[0].scriptPubKey = a_and_b;
    txFrom.vout[1].scriptPubKey = a_or_b;
    txFrom.vout[2].scriptPubKey = escrow;

    CMutableTransaction txTo[3]; // Spending transaction
    for (int i = 0; i < 3; i++) {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1;
    }

    std::vector<CKey> keys;
    CScript s;
    Script::State state(flags);

    // Test a AND b:
    keys.assign(1,key[0]);
    keys.push_back(key[1]);
    s = sign_multisig(a_and_b, keys, txTo[0], 0);
    QVERIFY(Script::verify(s, a_and_b, MutableTransactionSignatureChecker(&txTo[0], 0, amount), state));
    QVERIFY2(state.error == SCRIPT_ERR_OK, state.errorString());

    for (int i = 0; i < 4; i++) {
        keys.assign(1,key[i]);
        s = sign_multisig(a_and_b, keys, txTo[0], 0);
        QVERIFY2(!Script::verify(s, a_and_b, MutableTransactionSignatureChecker(&txTo[0], 0, amount), state), strprintf("a&b 1: %d", i).c_str());
        QVERIFY2(state.error == SCRIPT_ERR_INVALID_STACK_OPERATION, state.errorString());

        keys.assign(1,key[1]);
        keys.push_back(key[i]);
        s = sign_multisig(a_and_b, keys, txTo[0], 0);
        QVERIFY2(!Script::verify(s, a_and_b, MutableTransactionSignatureChecker(&txTo[0], 0, amount), state), strprintf("a&b 2: %d", i).c_str());
        QVERIFY2(state.error == SCRIPT_ERR_EVAL_FALSE, state.errorString());
    }

    // Test a OR b:
    for (int i = 0; i < 4; i++) {
        keys.assign(1,key[i]);
        s = sign_multisig(a_or_b, keys, txTo[1], 0);
        if (i == 0 || i == 1)
        {
            QVERIFY2(Script::verify(s, a_or_b, MutableTransactionSignatureChecker(&txTo[1], 0, amount), state), strprintf("a|b: %d", i).c_str());
            QVERIFY2(state.error == SCRIPT_ERR_OK, state.errorString());
        }
        else
        {
            QVERIFY2(!Script::verify(s, a_or_b, MutableTransactionSignatureChecker(&txTo[1], 0, amount), state), strprintf("a|b: %d", i).c_str());
            QVERIFY2(state.error == SCRIPT_ERR_EVAL_FALSE, state.errorString());
        }
    }
    s.clear();
    s << OP_0 << OP_1;
    QVERIFY(!Script::verify(s, a_or_b, MutableTransactionSignatureChecker(&txTo[1], 0, amount), state));
    QVERIFY2(state.error == SCRIPT_ERR_SIG_DER, state.errorString());


    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
        {
            keys.assign(1,key[i]);
            keys.push_back(key[j]);
            s = sign_multisig(escrow, keys, txTo[2], 0);
            if (i < j && i < 3 && j < 3)
            {
                QVERIFY2(Script::verify(s, escrow, MutableTransactionSignatureChecker(&txTo[2], 0, amount), state), strprintf("escrow 1: %d %d", i, j).c_str());
                QVERIFY2(state.error == SCRIPT_ERR_OK, state.errorString());
            }
            else {
                QVERIFY2(!Script::verify(s, escrow, MutableTransactionSignatureChecker(&txTo[2], 0, amount), state), strprintf("escrow 2: %d %d", i, j).c_str());
                QVERIFY2(state.error == SCRIPT_ERR_EVAL_FALSE, state.errorString());
            }
        }
}

void MultiSigTests::multisig_IsStandard()
{
    CKey key[4];
    for (int i = 0; i < 4; i++)
        key[i].MakeNewKey(true);

    Script::TxnOutType whichType;

    CScript a_and_b;
    a_and_b << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    QVERIFY(::IsStandard(a_and_b, whichType));

    CScript a_or_b;
    a_or_b  << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    QVERIFY(::IsStandard(a_or_b, whichType));

    CScript escrow;
    escrow << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;
    QVERIFY(::IsStandard(escrow, whichType));

    CScript one_of_four;
    one_of_four << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << ToByteVector(key[3].GetPubKey()) << OP_4 << OP_CHECKMULTISIG;
    QVERIFY(!::IsStandard(one_of_four, whichType));

    CScript malformed[6];
    malformed[0] << OP_3 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    malformed[1] << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;
    malformed[2] << OP_0 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    malformed[3] << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_0 << OP_CHECKMULTISIG;
    malformed[4] << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_CHECKMULTISIG;
    malformed[5] << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey());

    for (int i = 0; i < 6; i++)
        QVERIFY(!::IsStandard(malformed[i], whichType));
}

void MultiSigTests::multisig_Solver1()
{
    // Tests Solver() that returns lists of keys that are
    // required to satisfy a ScriptPubKey
    //
    // Also tests IsMine() and ExtractDestination()
    //
    // Note: ExtractDestination for the multisignature transactions
    // always returns false for this release, even if you have
    // one key that would satisfy an (a|b) or 2-of-3 keys needed
    // to spend an escrow transaction.
    //
    CBasicKeyStore keystore, emptykeystore, partialkeystore;
    CKey key[3];
    CTxDestination keyaddr[3];
    for (int i = 0; i < 3; i++) {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
        keyaddr[i] = key[i].GetPubKey().GetID();
    }
    partialkeystore.AddKey(key[0]);

    {
        std::vector<valtype> solutions;
        Script::TxnOutType whichType;
        CScript s;
        s << ToByteVector(key[0].GetPubKey()) << OP_CHECKSIG;
        QVERIFY(Script::solver(s, whichType, solutions));
        QVERIFY(solutions.size() == 1);
        CTxDestination addr;
        QVERIFY(ExtractDestination(s, addr));
        QVERIFY(addr == keyaddr[0]);
#ifdef ENABLE_WALLET
        QVERIFY(IsMine(keystore, s));
        QVERIFY(!IsMine(emptykeystore, s));
#endif
    }
    {
        std::vector<valtype> solutions;
        Script::TxnOutType whichType;
        CScript s;
        s << OP_DUP << OP_HASH160 << ToByteVector(key[0].GetPubKey().GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
        QVERIFY(Script::solver(s, whichType, solutions));
        QVERIFY(solutions.size() == 1);
        CTxDestination addr;
        QVERIFY(ExtractDestination(s, addr));
        QVERIFY(addr == keyaddr[0]);
#ifdef ENABLE_WALLET
        QVERIFY(IsMine(keystore, s));
        QVERIFY(!IsMine(emptykeystore, s));
#endif
    }
    {
        std::vector<valtype> solutions;
        Script::TxnOutType whichType;
        CScript s;
        s << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
        QVERIFY(Script::solver(s, whichType, solutions));
        QCOMPARE(solutions.size(), 4U);
        CTxDestination addr;
        QVERIFY(!ExtractDestination(s, addr));
#ifdef ENABLE_WALLET
        QVERIFY(IsMine(keystore, s));
        QVERIFY(!IsMine(emptykeystore, s));
        QVERIFY(!IsMine(partialkeystore, s));
#endif
    }
    {
        std::vector<valtype> solutions;
        Script::TxnOutType whichType;
        CScript s;
        s << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
        QVERIFY(Script::solver(s, whichType, solutions));
        QCOMPARE(solutions.size(), 4U);
        std::vector<CTxDestination> addrs;
        int nRequired;
        QVERIFY(ExtractDestinations(s, whichType, addrs, nRequired));
        QVERIFY(addrs[0] == keyaddr[0]);
        QVERIFY(addrs[1] == keyaddr[1]);
        QVERIFY(nRequired == 1);
#ifdef ENABLE_WALLET
        QVERIFY(IsMine(keystore, s));
        QVERIFY(!IsMine(emptykeystore, s));
        QVERIFY(!IsMine(partialkeystore, s));
#endif
    }
    {
        std::vector<valtype> solutions;
        Script::TxnOutType whichType;
        CScript s;
        s << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;
        QVERIFY(Script::solver(s, whichType, solutions));
        QVERIFY(solutions.size() == 5);
    }
}

void MultiSigTests::multisig_Sign()
{
    // Test SignSignature() (and therefore the version of Solver() that signs transactions)
    CBasicKeyStore keystore;
    CKey key[4];
    for (int i = 0; i < 4; i++) {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
    }

    CScript a_and_b;
    a_and_b << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript a_or_b;
    a_or_b  << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript escrow;
    escrow << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;

    CMutableTransaction txFrom;  // Funding transaction
    txFrom.vout.resize(3);
    txFrom.vout[0].scriptPubKey = a_and_b;
    txFrom.vout[1].scriptPubKey = a_or_b;
    txFrom.vout[2].scriptPubKey = escrow;

    CMutableTransaction txTo[3]; // Spending transaction
    for (int i = 0; i < 3; i++) {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1;
    }

    for (int i = 0; i < 3; i++) {
        QVERIFY2(SignSignature(keystore, txFrom, txTo[i], 0), strprintf("SignSignature %d", i).c_str());
    }
}
