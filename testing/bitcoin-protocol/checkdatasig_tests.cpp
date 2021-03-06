/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tom@flowee.org>
 * Copyright (C) 2018 Amaury Séchet <deadalnix@gmail.com>
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

#include "checkdatasig_tests.h"
#include <hash.h>
#include <script/interpreter.h>
#include <policy/policy.h>

typedef std::vector<uint8_t> valtype;
typedef std::vector<valtype> stacktype;

std::array<uint32_t, 3> flagset{{0, STANDARD_SCRIPT_VERIFY_FLAGS, MANDATORY_SCRIPT_VERIFY_FLAGS}};

const uint8_t vchPrivkey[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

KeyData::KeyData() {
    privkey.Set(vchPrivkey, vchPrivkey + 32, false);
    privkeyC.Set(vchPrivkey, vchPrivkey + 32, true);
    pubkey = privkey.GetPubKey();
    pubkeyH = privkey.GetPubKey();
    pubkeyC = privkeyC.GetPubKey();
    *const_cast<uint8_t *>(&pubkeyH[0]) = 0x06 | (pubkeyH[64] & 1);
}

static void CheckError(uint32_t flags, const stacktype &original_stack, const CScript &script, ScriptError expected)
{
    BaseSignatureChecker sigchecker;
    Script::State state(flags);
    stacktype stack{original_stack};
    bool r = Script::eval(stack, script, sigchecker, state);
    QVERIFY(!r);
    QCOMPARE(state.error, expected);
}

static void CheckPass(uint32_t flags, const stacktype &original_stack, const CScript &script, const stacktype &expected)
{
    BaseSignatureChecker sigchecker;
    Script::State state(flags);
    stacktype stack{original_stack};
    bool r = Script::eval(stack, script, sigchecker, state);
    QVERIFY(r);
    QCOMPARE(state.error, SCRIPT_ERR_OK);
    QVERIFY(stack == expected);
}

/**
 * General utility functions to check for script passing/failing.
 */
static void CheckTestResultForAllFlags(const stacktype &original_stack,
    const CScript &script,
    const stacktype &expected)
{
    for (uint32_t flags : flagset) {
        flags += SCRIPT_ENABLE_SIGHASH_FORKID;
        // Make sure that we get a bad opcode when the activation flag is not
        // passed.
        CheckError(flags, original_stack, script, SCRIPT_ERR_BAD_OPCODE);

        // The script execute as expected if the opcodes are activated.
        CheckPass(flags | SCRIPT_ENABLE_CHECKDATASIG, original_stack, script, expected);
    }
}

static void CheckErrorForAllFlags(const stacktype &original_stack, const CScript &script, ScriptError expected)
{
    for (uint32_t flags : flagset) {
        // Make sure that we get a bad opcode when the activation flag is not
        // passed.
        CheckError(flags, original_stack, script, SCRIPT_ERR_BAD_OPCODE);

        // The script generates the proper error if the opcodes are activated.
        CheckError(flags | SCRIPT_ENABLE_CHECKDATASIG, original_stack, script, expected);
    }
}

void CheckDataSig::checkdatasig_test()
{
    // Empty stack.
    CheckErrorForAllFlags({}, CScript() << OP_CHECKDATASIG, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckErrorForAllFlags({{0x00}}, CScript() << OP_CHECKDATASIG, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckErrorForAllFlags({{0x00}, {0x00}}, CScript() << OP_CHECKDATASIG, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckErrorForAllFlags({}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckErrorForAllFlags({{0x00}}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckErrorForAllFlags({{0x00}, {0x00}}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_INVALID_STACK_OPERATION);

    // Check various pubkey encoding.
    const valtype message{};
    valtype vchHash(32);
    CSHA256().Write(message.data(), message.size()).Finalize(vchHash.data());
    uint256 messageHash(vchHash);

    KeyData kd;
    valtype pubkey = ToByteVector(kd.pubkey);
    valtype pubkeyC = ToByteVector(kd.pubkeyC);
    valtype pubkeyH = ToByteVector(kd.pubkeyH);

    CheckTestResultForAllFlags({{}, message, pubkey}, CScript() << OP_CHECKDATASIG, {{}});
    CheckTestResultForAllFlags({{}, message, pubkeyC}, CScript() << OP_CHECKDATASIG, {{}});
    CheckErrorForAllFlags({{}, message, pubkey}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_CHECKDATASIGVERIFY);
    CheckErrorForAllFlags({{}, message, pubkeyC}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_CHECKDATASIGVERIFY);

    // Flags dependent checks.
    const CScript script = CScript() << OP_CHECKDATASIG << OP_NOT << OP_VERIFY;
    const CScript scriptverify = CScript() << OP_CHECKDATASIGVERIFY;

    // Check valid signatures (as in the signature format is valid).
    valtype validsig;
    kd.privkey.signECDSA(messageHash, validsig);

    CheckTestResultForAllFlags({validsig, message, pubkey}, CScript() << OP_CHECKDATASIG, {{0x01}});
    CheckTestResultForAllFlags({validsig, message, pubkey}, CScript() << OP_CHECKDATASIGVERIFY, {});

    const valtype minimalsig{0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01};
    const valtype nondersig{0x30, 0x80, 0x06, 0x02, 0x01,
                            0x01, 0x02, 0x01, 0x01};
    const valtype highSSig{
        0x30, 0x45, 0x02, 0x20, 0x3e, 0x45, 0x16, 0xda, 0x72, 0x53, 0xcf, 0x06,
        0x8e, 0xff, 0xec, 0x6b, 0x95, 0xc4, 0x12, 0x21, 0xc0, 0xcf, 0x3a, 0x8e,
        0x6c, 0xcb, 0x8c, 0xbf, 0x17, 0x25, 0xb5, 0x62, 0xe9, 0xaf, 0xde, 0x2c,
        0x02, 0x21, 0x00, 0xab, 0x1e, 0x3d, 0xa7, 0x3d, 0x67, 0xe3, 0x20, 0x45,
        0xa2, 0x0e, 0x0b, 0x99, 0x9e, 0x04, 0x99, 0x78, 0xea, 0x8d, 0x6e, 0xe5,
        0x48, 0x0d, 0x48, 0x5f, 0xcf, 0x2c, 0xe0, 0xd0, 0x3b, 0x2e, 0xf0};


    const uint32_t flagsArray[] {
        SCRIPT_VERIFY_NONE,
        SCRIPT_VERIFY_STRICTENC,
        SCRIPT_VERIFY_STRICTENC | SCRIPT_VERIFY_DERSIG,
        SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_STRICTENC,
        SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_STRICTENC | SCRIPT_VERIFY_DERSIG,
        SCRIPT_VERIFY_NULLFAIL | SCRIPT_VERIFY_STRICTENC,
        SCRIPT_VERIFY_NULLFAIL | SCRIPT_VERIFY_STRICTENC | SCRIPT_VERIFY_DERSIG,
        SCRIPT_VERIFY_NULLFAIL | SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_STRICTENC,
        SCRIPT_VERIFY_NULLFAIL | SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_STRICTENC | SCRIPT_VERIFY_DERSIG
    };

    // If OP_CHECKDATASIG* are allowed.
    for (auto f : flagsArray) {
        // Make sure we activate the opcodes.
        const uint32_t flags = f | SCRIPT_ENABLE_CHECKDATASIG | SCRIPT_ENABLE_SIGHASH_FORKID;

        if (flags & SCRIPT_VERIFY_STRICTENC) {
            // When strict encoding is enforced, hybrid key are invalid.
            CheckError(flags, {{}, message, pubkeyH}, script, SCRIPT_ERR_PUBKEYTYPE);
            CheckError(flags, {{}, message, pubkeyH}, scriptverify, SCRIPT_ERR_PUBKEYTYPE);
        } else {
            // When strict encoding is not enforced, hybrid key are valid.
            CheckPass(flags, {{}, message, pubkeyH}, script, {});
            CheckError(flags, {{}, message, pubkeyH}, scriptverify, SCRIPT_ERR_CHECKDATASIGVERIFY);
        }

        if (flags & SCRIPT_VERIFY_NULLFAIL) {
            // When strict encoding is enforced, hybrid key are invalid.
            CheckError(flags, {minimalsig, message, pubkey}, script, SCRIPT_ERR_SIG_NULLFAIL);
            CheckError(flags, {minimalsig, message, pubkey}, scriptverify, SCRIPT_ERR_SIG_NULLFAIL);

            // Invalid message cause checkdatasig to fail.
            CheckError(flags, {validsig, {0x01}, pubkey}, script, SCRIPT_ERR_SIG_NULLFAIL);
            CheckError(flags, {validsig, {0x01}, pubkey}, scriptverify, SCRIPT_ERR_SIG_NULLFAIL);
        } else {
            // When nullfail is not enforced, invalid signature are just false.
            CheckPass(flags, {minimalsig, message, pubkey}, script, {});
            CheckError(flags, {minimalsig, message, pubkey}, scriptverify, SCRIPT_ERR_CHECKDATASIGVERIFY);

            // Invalid message cause checkdatasig to fail.
            CheckPass(flags, {validsig, {0x01}, pubkey}, script, {});
            CheckError(flags, {validsig, {0x01}, pubkey}, scriptverify, SCRIPT_ERR_CHECKDATASIGVERIFY);
        }

        // Make sure we activate the opcodes.
        if (flags & SCRIPT_VERIFY_LOW_S) {
            // If we do enforce low S, then high S sigs are rejected.
            CheckError(flags, {highSSig, message, pubkey}, script, SCRIPT_ERR_SIG_HIGH_S);
            CheckError(flags, {highSSig, message, pubkey}, scriptverify, SCRIPT_ERR_SIG_HIGH_S);
        } else if (flags & SCRIPT_VERIFY_NULLFAIL) {
            // If we do enforce low S, and NULLFAIL, then we return with SCRIPT_ERR_SIG_NULLFAIL
            CheckError(flags, {highSSig, message, pubkey}, script, SCRIPT_ERR_SIG_NULLFAIL);
            CheckError(flags, {highSSig, message, pubkey}, scriptverify, SCRIPT_ERR_SIG_NULLFAIL);
        } else {
            // If we do not enforce low S, then high S sigs are accepted.
            CheckPass(flags, {highSSig, message, pubkey}, script, {});
            CheckError(flags, {highSSig, message, pubkey}, scriptverify, SCRIPT_ERR_CHECKDATASIGVERIFY);
        }

        if (flags & (SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_STRICTENC)) {
            // If we get any of the dersig flags, the non canonical dersig
            // signature fails.
            CheckError(flags, {nondersig, message, pubkey}, script, SCRIPT_ERR_SIG_DER);
            CheckError(flags, {nondersig, message, pubkey}, scriptverify, SCRIPT_ERR_SIG_DER);
        } else {
            // If we do not check, then it is accepted.
            CheckPass(flags, {nondersig, message, pubkey}, script, {});
            CheckError(flags, {nondersig, message, pubkey}, scriptverify, SCRIPT_ERR_CHECKDATASIGVERIFY);
        }
    }
}

void CheckDataSig::checkdatasig_opcode_formatting()
{
    QCOMPARE(GetOpName(OP_CHECKDATASIG), "OP_CHECKDATASIG");
    QCOMPARE(GetOpName(OP_CHECKDATASIGVERIFY), "OP_CHECKDATASIGVERIFY");
}
