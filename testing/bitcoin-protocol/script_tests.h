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

#ifndef SCRIPT_TESTS_H
#define SCRIPT_TESTS_H
#include <primitives/transaction.h>
#include <primitives/key.h>
#include <univalue.h>
#include <script/interpreter.h>

#include <common/TestFloweeEnvPlusNet.h>

class TestBuilder
{
private:
    CScript scriptPubKey;
    CTransaction creditTx;
    CMutableTransaction spendTx;
    bool havePush;
    std::vector<unsigned char> push;
    QString comment;
    int flags;
    CAmount nValue;

    void DoPush();

    void DoPush(const std::vector<unsigned char>& data);

public:
    TestBuilder(const CScript& redeemScript, const QString& comment, int flags, bool P2SH = false, CAmount nValue_ = 0);

    TestBuilder& Add(const CScript& script);
    TestBuilder& Num(int num);
    TestBuilder& Push(const std::string& hex);

    TestBuilder& PushSig(const CKey& key, int nHashType = SIGHASH_ALL, unsigned int lenR = 32, unsigned int lenS = 32, CAmount amount = 0);

    TestBuilder& Push(const CPubKey& pubkey);

    TestBuilder& PushRedeem();

    TestBuilder& EditPush(unsigned int pos, const std::string& hexin, const std::string& hexout);

    TestBuilder& DamagePush(unsigned int pos);

    TestBuilder& Test(bool expect);

    UniValue GetJSON();

    QString GetComment() const;

    const CScript& GetScriptPubKey();
};

class TestScript : public TestFloweeEnvPlusNet
{
    Q_OBJECT
public:
    CScript sign_multisig(CScript scriptPubKey, std::vector<CKey> keys, CTransaction transaction);
    CScript sign_multisig(CScript scriptPubKey, const CKey &key, CTransaction transaction);

private slots:
    void script_build();
    void script_valid();
    void script_invalid();
    void script_PushData();
    void script_CHECKMULTISIG12();
    void script_CHECKMULTISIG23();
    void script_combineSigs();
    void script_standard_push();
    void script_IsPushOnly_on_invalid_scripts();
    void script_GetScriptAsm();
    void minimize_big_endian_test();
};

#endif
