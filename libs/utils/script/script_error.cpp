/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2014 The Bitcoin Core developers
 * Copyright (C) 2018 Awemany <awemany@protonmail.com>
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

#include "script_error.h"

const char* ScriptErrorString(const ScriptError serror)
{
    switch (serror)
    {
        case SCRIPT_ERR_OK:
            return "No error";
        case SCRIPT_ERR_EVAL_FALSE:
            return "Script evaluated without error but finished with a false/empty top stack element";
        case SCRIPT_ERR_VERIFY:
            return "Script failed an OP_VERIFY operation";
        case SCRIPT_ERR_EQUALVERIFY:
            return "Script failed an OP_EQUALVERIFY operation";
        case SCRIPT_ERR_CHECKMULTISIGVERIFY:
            return "Script failed an OP_CHECKMULTISIGVERIFY operation";
        case SCRIPT_ERR_CHECKSIGVERIFY:
            return "Script failed an OP_CHECKSIGVERIFY operation";
        case SCRIPT_ERR_CHECKDATASIGVERIFY:
            return "Script failed an OP_CHECKDATASIGVERIFY operation";
        case SCRIPT_ERR_NUMEQUALVERIFY:
            return "Script failed an OP_NUMEQUALVERIFY operation";
        case SCRIPT_ERR_SCRIPT_SIZE:
            return "Script is too big";
        case SCRIPT_ERR_PUSH_SIZE:
            return "Push value size limit exceeded";
        case SCRIPT_ERR_OP_COUNT:
            return "Operation limit exceeded";
        case SCRIPT_ERR_STACK_SIZE:
            return "Stack size limit exceeded";
        case SCRIPT_ERR_SIG_COUNT:
            return "Signature count negative or greater than pubkey count";
        case SCRIPT_ERR_PUBKEY_COUNT:
            return "Pubkey count negative or limit exceeded";
        case SCRIPT_ERR_INVALID_OPERAND_SIZE:
            return "Invalid operand size";
        case SCRIPT_ERR_INVALID_NUMBER_RANGE:
            return "Given operand is not a number within the valid range [-2^31...2^31]";
        case SCRIPT_ERR_IMPOSSIBLE_ENCODING:
            return "The requested encoding is impossible to satisfy";
        case SCRIPT_ERR_INVALID_SPLIT_RANGE:
            return "Invalid OP_SPLIT range";
        case SCRIPT_ERR_BAD_OPCODE:
            return "Opcode missing or not understood";
        case SCRIPT_ERR_DISABLED_OPCODE:
            return "Attempted to use a disabled opcode";
        case SCRIPT_ERR_INVALID_STACK_OPERATION:
            return "Operation not valid with the current stack size";
        case SCRIPT_ERR_INVALID_ALTSTACK_OPERATION:
            return "Operation not valid with the current altstack size";
        case SCRIPT_ERR_OP_RETURN:
            return "OP_RETURN was encountered";
        case SCRIPT_ERR_UNBALANCED_CONDITIONAL:
            return "Invalid OP_IF construction";
        case SCRIPT_ERR_DIV_BY_ZERO:
            return "Division by zero error";
        case SCRIPT_ERR_MOD_BY_ZERO:
            return "Modulo by zero error";
        case SCRIPT_ERR_NEGATIVE_LOCKTIME:
            return "Negative locktime";
        case SCRIPT_ERR_UNSATISFIED_LOCKTIME:
            return "Locktime requirement not satisfied";
        case SCRIPT_ERR_SIG_HASHTYPE:
            return "Signature hash type missing or not understood";
        case SCRIPT_ERR_SIG_DER:
            return "Non-canonical DER signature";
        case SCRIPT_ERR_MINIMALDATA:
            return "Data push larger than necessary";
        case SCRIPT_ERR_SIG_PUSHONLY:
            return "Only non-push operators allowed in signatures";
        case SCRIPT_ERR_SIG_HIGH_S:
            return "Non-canonical signature: S value is unnecessarily high";
        case SCRIPT_ERR_SIG_NULLDUMMY:
            return "Dummy CHECKMULTISIG argument must be zero";
        case SCRIPT_ERR_SIG_NULLFAIL:
            return "Signature must be zero for failed CHECK(MULTI)SIG operation";
        case SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS:
            return "NOPx reserved for soft-fork upgrades";
        case SCRIPT_ERR_PUBKEYTYPE:
            return "Public key is neither compressed or uncompressed";
        case SCRIPT_ERR_ILLEGAL_FORKID:
            return "Illegal use of SIGHASH_FORKID";
        case SCRIPT_ERR_MUST_USE_FORKID:
            return "Signature must use SIGHASH_FORKID";
        case SCRIPT_ERR_SIG_BADLENGTH:
            return "Signature cannot be 65 bytes in CHECKMULTISIG";
        case SCRIPT_ERR_INVALID_BIT_COUNT:
            return "Invalid number of bits set in OP_CHECKMULTISIG";
        case SCRIPT_ERR_INVALID_BIT_RANGE:
            return "Bitfield's bit out of the expected range";
        case SCRIPT_ERR_INVALID_BITFIELD_SIZE:
            return "Bitfield of unexpected size error";
        case SCRIPT_ERR_UNKNOWN_ERROR:
        case SCRIPT_ERR_ERROR_COUNT:
        default: break;
    }
    return "unknown error";
}