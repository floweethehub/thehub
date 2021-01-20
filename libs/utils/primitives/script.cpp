/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2015 The Bitcoin Core developers
 * Copyright (C) 2018 Jason B. Cox <contact@jasonbcox.com>
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

#include "script.h"
#include "pubkey_utils.h"

#include "tinyformat.h"
#include "utilstrencodings.h"

#include <uint256.h>

#include <map>

const char* GetOpName(opcodetype opcode)
{
    switch (opcode)
    {
    // push value
    case OP_0                      : return "0";
    case OP_PUSHDATA1              : return "OP_PUSHDATA1";
    case OP_PUSHDATA2              : return "OP_PUSHDATA2";
    case OP_PUSHDATA4              : return "OP_PUSHDATA4";
    case OP_1NEGATE                : return "-1";
    case OP_RESERVED               : return "OP_RESERVED";
    case OP_1                      : return "1";
    case OP_2                      : return "2";
    case OP_3                      : return "3";
    case OP_4                      : return "4";
    case OP_5                      : return "5";
    case OP_6                      : return "6";
    case OP_7                      : return "7";
    case OP_8                      : return "8";
    case OP_9                      : return "9";
    case OP_10                     : return "10";
    case OP_11                     : return "11";
    case OP_12                     : return "12";
    case OP_13                     : return "13";
    case OP_14                     : return "14";
    case OP_15                     : return "15";
    case OP_16                     : return "16";

    // control
    case OP_NOP                    : return "OP_NOP";
    case OP_VER                    : return "OP_VER";
    case OP_IF                     : return "OP_IF";
    case OP_NOTIF                  : return "OP_NOTIF";
    case OP_VERIF                  : return "OP_VERIF";
    case OP_VERNOTIF               : return "OP_VERNOTIF";
    case OP_ELSE                   : return "OP_ELSE";
    case OP_ENDIF                  : return "OP_ENDIF";
    case OP_VERIFY                 : return "OP_VERIFY";
    case OP_RETURN                 : return "OP_RETURN";

    // stack ops
    case OP_TOALTSTACK             : return "OP_TOALTSTACK";
    case OP_FROMALTSTACK           : return "OP_FROMALTSTACK";
    case OP_2DROP                  : return "OP_2DROP";
    case OP_2DUP                   : return "OP_2DUP";
    case OP_3DUP                   : return "OP_3DUP";
    case OP_2OVER                  : return "OP_2OVER";
    case OP_2ROT                   : return "OP_2ROT";
    case OP_2SWAP                  : return "OP_2SWAP";
    case OP_IFDUP                  : return "OP_IFDUP";
    case OP_DEPTH                  : return "OP_DEPTH";
    case OP_DROP                   : return "OP_DROP";
    case OP_DUP                    : return "OP_DUP";
    case OP_NIP                    : return "OP_NIP";
    case OP_OVER                   : return "OP_OVER";
    case OP_PICK                   : return "OP_PICK";
    case OP_ROLL                   : return "OP_ROLL";
    case OP_ROT                    : return "OP_ROT";
    case OP_SWAP                   : return "OP_SWAP";
    case OP_TUCK                   : return "OP_TUCK";

    // splice ops
    case OP_CAT                    : return "OP_CAT";
    case OP_SPLIT                  : return "OP_SPLIT";
    case OP_NUM2BIN                : return "OP_NUM2BIN";
    case OP_BIN2NUM                  : return "OP_BIN2NUM";
    case OP_SIZE                   : return "OP_SIZE";

    // bit logic
    case OP_INVERT                 : return "OP_INVERT";
    case OP_AND                    : return "OP_AND";
    case OP_OR                     : return "OP_OR";
    case OP_XOR                    : return "OP_XOR";
    case OP_EQUAL                  : return "OP_EQUAL";
    case OP_EQUALVERIFY            : return "OP_EQUALVERIFY";
    case OP_RESERVED1              : return "OP_RESERVED1";
    case OP_RESERVED2              : return "OP_RESERVED2";

    // numeric
    case OP_1ADD                   : return "OP_1ADD";
    case OP_1SUB                   : return "OP_1SUB";
    case OP_2MUL                   : return "OP_2MUL";
    case OP_2DIV                   : return "OP_2DIV";
    case OP_NEGATE                 : return "OP_NEGATE";
    case OP_ABS                    : return "OP_ABS";
    case OP_NOT                    : return "OP_NOT";
    case OP_0NOTEQUAL              : return "OP_0NOTEQUAL";
    case OP_ADD                    : return "OP_ADD";
    case OP_SUB                    : return "OP_SUB";
    case OP_MUL                    : return "OP_MUL";
    case OP_DIV                    : return "OP_DIV";
    case OP_MOD                    : return "OP_MOD";
    case OP_LSHIFT                 : return "OP_LSHIFT";
    case OP_RSHIFT                 : return "OP_RSHIFT";
    case OP_BOOLAND                : return "OP_BOOLAND";
    case OP_BOOLOR                 : return "OP_BOOLOR";
    case OP_NUMEQUAL               : return "OP_NUMEQUAL";
    case OP_NUMEQUALVERIFY         : return "OP_NUMEQUALVERIFY";
    case OP_NUMNOTEQUAL            : return "OP_NUMNOTEQUAL";
    case OP_LESSTHAN               : return "OP_LESSTHAN";
    case OP_GREATERTHAN            : return "OP_GREATERTHAN";
    case OP_LESSTHANOREQUAL        : return "OP_LESSTHANOREQUAL";
    case OP_GREATERTHANOREQUAL     : return "OP_GREATERTHANOREQUAL";
    case OP_MIN                    : return "OP_MIN";
    case OP_MAX                    : return "OP_MAX";
    case OP_WITHIN                 : return "OP_WITHIN";

    // crypto
    case OP_RIPEMD160              : return "OP_RIPEMD160";
    case OP_SHA1                   : return "OP_SHA1";
    case OP_SHA256                 : return "OP_SHA256";
    case OP_HASH160                : return "OP_HASH160";
    case OP_HASH256                : return "OP_HASH256";
    case OP_CODESEPARATOR          : return "OP_CODESEPARATOR";
    case OP_CHECKSIG               : return "OP_CHECKSIG";
    case OP_CHECKSIGVERIFY         : return "OP_CHECKSIGVERIFY";
    case OP_CHECKMULTISIG          : return "OP_CHECKMULTISIG";
    case OP_CHECKMULTISIGVERIFY    : return "OP_CHECKMULTISIGVERIFY";

    // soft-forkable expansion
    case OP_NOP1                   : return "OP_NOP1";
    case OP_CHECKLOCKTIMEVERIFY    : return "OP_CHECKLOCKTIMEVERIFY";
    case OP_NOP3                   : return "OP_NOP3";
    case OP_NOP4                   : return "OP_NOP4";
    case OP_NOP5                   : return "OP_NOP5";
    case OP_NOP6                   : return "OP_NOP6";
    case OP_NOP7                   : return "OP_NOP7";
    case OP_NOP8                   : return "OP_NOP8";
    case OP_NOP9                   : return "OP_NOP9";
    case OP_NOP10                  : return "OP_NOP10";

    // new opcodes hard-forked in.
    case OP_CHECKDATASIG: return "OP_CHECKDATASIG";
    case OP_CHECKDATASIGVERIFY: return "OP_CHECKDATASIGVERIFY";
    case OP_REVERSEBYTES: return "OP_REVERSEBYTES";

    case OP_INVALIDOPCODE          : return "OP_INVALIDOPCODE";

    // Note:
    //  The template matching params OP_SMALLINTEGER/etc are defined in opcodetype enum
    //  as kind of implementation hack, they are *NOT* real opcodes.  If found in real
    //  Script, just let the default: case deal with them.

    default:
        return "OP_UNKNOWN";
    }
}

bool CheckMinimalPush(const std::vector<uint8_t> &data, opcodetype opcode)
{
    // Excludes OP_1NEGATE, OP_1-16 since they are by definition minimal.

    if (opcode <= OP_0 || opcode > OP_PUSHDATA4)
        // Anything outside of this range has nothing to do with data push
        return true;
    if (data.size() == 0) // Should use OP_0.
        return opcode == OP_0;
    if (data.size() == 1 && data[0] >= 1 && data[0] <= 16) // Should use OP_1 .. OP_16.
        return false;
    if (data.size() == 1 && data[0] == 0x81) // Should use OP_1NEGATE.
        return false;
    if (data.size() <= 75)
        // Should use a direct push (opcode indicating number of bytes pushed + those bytes).
        return opcode == data.size();
    if (data.size() <= 255) // Should use OP_PUSHDATA.
        return opcode == OP_PUSHDATA1;
    if (data.size() <= 65535) // Should use OP_PUSHDATA2.
        return opcode == OP_PUSHDATA2;
    return true;
}

bool CScript::IsPayToScriptHash() const
{
    // Extra-fast test for pay-to-script-hash CScripts:
    return (this->size() == 23 &&
            (*this)[0] == OP_HASH160 &&
            (*this)[1] == 0x14 &&
            (*this)[22] == OP_EQUAL);
}

// A witness program is any valid CScript that consists of a 1-byte push opcode
// followed by a data push between 2 and 40 bytes.
bool CScript::IsWitnessProgram() const
{
    if (this->size() < 4 || this->size() > 42)
        return false;
    if ((*this)[0] != OP_0 && ((*this)[0] < OP_1 || (*this)[0] > OP_16))
        return false;
    if (size_t((*this)[1] + 2) == this->size())
        return true;
    return false;
}

bool CScript::IsPushOnly(const_iterator pc) const
{
    while (pc < end())
    {
        opcodetype opcode;
        if (!GetOp(pc, opcode))
            return false;
        // Note that IsPushOnly() *does* consider OP_RESERVED to be a
        // push-type opcode, however execution of OP_RESERVED fails, so
        // it's not relevant to P2SH/BIP62 as the scriptSig would fail prior to
        // the P2SH special validation code being executed.
        if (opcode > OP_16)
            return false;
    }
    return true;
}

bool CScript::IsPushOnly() const
{
    return this->IsPushOnly(begin());
}

std::vector<uint8_t>

MinimalizeBigEndianArray(const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> answer;
    // Can't encode more than this, go ahead and grab as much room as we could
    // possibly need
    answer.reserve(data.size());

    if (data.empty()) // Ensure we have a byte to work with.
        return answer;

    // Store the MSB
    uint8_t neg = data[0] & 0x80;
    bool havePushed = false;
    for (size_t i = 0; i < data.size(); ++i) {
        uint8_t x = data[i];
        if (i == 0) // Remove any MSB that might exist
            x &= 0x7f;

        if (!havePushed && x == 0) // If we haven't pushed anything, and the current value is zero, keep ignoring bytes.
            continue;

        // Record that we have begun pushing, and store the current value.
        havePushed = true;
        answer.push_back(x);
    }
    if (answer.size() == 0) // Give us at least one byte
        return answer;

    // Only add back the sign if a value has been pushed.  This implies the
    // result is non-zero.
    if (havePushed) {
        // If the MSB is currently occupied, we need one extra byte.
        if ((answer[0] & 0x80) != 0) {
            answer.insert(answer.begin(), 0);
        }
        answer[0] |= neg;
    }
    return answer;
}

bool CScriptNum::isSmallestFormat(const std::vector<uint8_t> &vch, const size_t nMaxNumSize)
{
    if (vch.size() > nMaxNumSize)
        return false;
    if (vch.size() > 0) {
        // If the most-significant-byte - excluding the sign bit - is zero
        // then we're not minimal. Note how this test also rejects the
        // negative-zero encoding, 0x80.
        if ((vch.back() & 0x7f) == 0) {
            // One exception: if there's more than one byte and the most
            // significant bit of the second-most-significant-byte is set it
            // would conflict with the sign bit. An example of this case is
            // +-255, which encode to 0xff00 and 0xff80 respectively.
            // (big-endian).
            if (vch.size() <= 1 || (vch[vch.size() - 2] & 0x80) == 0) {
                return false;
            }
        }
    }
    return true;
}

bool CScriptNum::createSmallestFormat(std::vector<uint8_t> &data)
{
    if (data.empty())
        return false;

    // If the last byte is not 0x00 or 0x80, we are minimally encoded.
    uint8_t last = data.back();
    if (last & 0x7f)
        return false;

    // If the script is one byte long, then we have a zero, which encodes as an
    // empty array.
    if (data.size() == 1) {
        data = {};
        return true;
    }

    // If the next byte has it sign bit set, then we are minimaly encoded.
    if (data[data.size() - 2] & 0x80)
        return false;

    // We are not minimally encoded, we need to figure out how much to trim.
    for (size_t i = data.size() - 1; i > 0; i--) {
        // We found a non zero byte, time to encode.
        if (data[i - 1] != 0) {
            if (data[i - 1] & 0x80) {
                // We found a byte with it sign bit set so we need one more
                // byte.
                data[i++] = last;
            } else {
                // the sign bit is clear, we can use it.
                data[i - 1] |= last;
            }

            data.resize(i);
            return true;
        }
    }

    // If we the whole thing is zeros, then we have a zero.
    data = {};
    return true;
}


const char* Script::getTxnOutputType(Script::TxnOutType t)
{
    switch (t)
    {
    case TX_NONSTANDARD: return "nonstandard";
    case TX_PUBKEY: return "pubkey";
    case TX_PUBKEYHASH: return "pubkeyhash";
    case TX_SCRIPTHASH: return "scripthash";
    case TX_MULTISIG: return "multisig";
    case TX_NULL_DATA: return "nulldata";
    }
    return nullptr;
}

struct SolverHelper {
    std::multimap<Script::TxnOutType, CScript> templates;
    SolverHelper() {
        // Standard tx, sender provides pubkey, receiver adds signature
        templates.insert(std::make_pair(Script::TX_PUBKEY, CScript() << OP_PUBKEY << OP_CHECKSIG));

        // Bitcoin address tx, sender provides hash of pubkey, receiver provides signature and pubkey
        templates.insert(std::make_pair(Script::TX_PUBKEYHASH, CScript() << OP_DUP << OP_HASH160
                                        << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG));

        // Sender provides N pubkeys, receivers provides M signatures
        templates.insert(std::make_pair(Script::TX_MULTISIG, CScript() << OP_SMALLINTEGER << OP_PUBKEYS
                                        << OP_SMALLINTEGER << OP_CHECKMULTISIG));
    }
};

/**
 * Return public keys or hashes from scriptPubKey, for 'standard' transaction types.
 */
bool Script::solver(const CScript &scriptPubKey, Script::TxnOutType &typeRet, std::vector<std::vector<unsigned char> > &vSolutionsRet)
{
    static SolverHelper sh;
    vSolutionsRet.clear();

    // Shortcut for pay-to-script-hash, which are more constrained than the other types:
    // it is always OP_HASH160 20 [20 byte hash] OP_EQUAL
    if (scriptPubKey.IsPayToScriptHash()) {
        typeRet = TX_SCRIPTHASH;
        std::vector<unsigned char> hashBytes(scriptPubKey.begin()+2, scriptPubKey.begin()+22);
        vSolutionsRet.push_back(hashBytes);
        return true;
    }

    // Provably prunable, data-carrying output
    //
    // So long as script passes the IsUnspendable() test and all but the first
    // byte passes the IsPushOnly() test we don't care what exactly is in the
    // script.
    if (scriptPubKey.size() >= 1 && scriptPubKey[0] == OP_RETURN && scriptPubKey.IsPushOnly(scriptPubKey.begin()+1)) {
        typeRet = TX_NULL_DATA;
        return true;
    }

    // Scan templates
    const CScript& script1 = scriptPubKey;
    for (const std::pair<const Script::TxnOutType, CScript> &tplate : sh.templates) {
        const CScript& script2 = tplate.second;
        vSolutionsRet.clear();

        opcodetype opcode1, opcode2;
        std::vector<unsigned char> vch1, vch2;

        // Compare
        CScript::const_iterator pc1 = script1.begin();
        CScript::const_iterator pc2 = script2.begin();
        while (true) {
            if (pc1 == script1.end() && pc2 == script2.end()) {
                // Found a match
                typeRet = tplate.first;
                if (typeRet == TX_MULTISIG) {
                    // Additional checks for TX_MULTISIG:
                    unsigned char m = vSolutionsRet.front()[0];
                    unsigned char n = vSolutionsRet.back()[0];
                    if (m < 1 || n < 1 || m > n || vSolutionsRet.size()-2 != n)
                        return false;
                }
                return true;
            }
            if (!script1.GetOp(pc1, opcode1, vch1))
                break;
            if (!script2.GetOp(pc2, opcode2, vch2))
                break;

            // Template matching opcodes:
            if (opcode2 == OP_PUBKEYS) {
                while (PubKey::isValidSize(vch1)) {
                    vSolutionsRet.push_back(vch1);
                    if (!script1.GetOp(pc1, opcode1, vch1))
                        break;
                }
                if (!script2.GetOp(pc2, opcode2, vch2))
                    break;
                // Normal situation is to fall through
                // to other if/else statements
            }

            if (opcode2 == OP_PUBKEY) {
                if (!PubKey::isValidSize(vch1))
                    break;
                vSolutionsRet.push_back(vch1);
            }
            else if (opcode2 == OP_PUBKEYHASH) {
                if (vch1.size() != sizeof(uint160))
                    break;
                vSolutionsRet.push_back(vch1);
            }
            else if (opcode2 == OP_SMALLINTEGER) {   // Single-byte small integer pushed onto vSolutions
                if (opcode1 == OP_0 || (opcode1 >= OP_1 && opcode1 <= OP_16)) {
                    unsigned char n = static_cast<unsigned char>(CScript::DecodeOP_N(opcode1));
                    vSolutionsRet.push_back(std::vector<unsigned char>(1, n));
                }
                else
                    break;
            }
            else if (opcode1 != opcode2 || vch1 != vch2) {
                // Others must match exactly
                break;
            }
        }
    }

    vSolutionsRet.clear();
    typeRet = TX_NONSTANDARD;
    return false;
}

