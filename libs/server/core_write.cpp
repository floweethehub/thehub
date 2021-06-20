/*
 * This file is part of the Flowee project
// Copyright (c) 2009-2015 The Bitcoin Core developers
 * Copyright (C) 2017 Tom Zander <tom@flowee.org>
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

#include "core_io.h"

#include <version.h>
#include <primitives/transaction.h>
#include <primitives/script.h>
#include "script/standard.h"
#include "serialize.h"
#include "streaming/streams.h"
#include <univalue.h>
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

const std::map<unsigned char, std::string> mapSigHashTypes =
    boost::assign::map_list_of
    (static_cast<unsigned char>(SIGHASH_ALL), std::string("ALL"))
    (static_cast<unsigned char>(SIGHASH_ALL|SIGHASH_ANYONECANPAY), std::string("ALL|ANYONECANPAY"))
    (static_cast<unsigned char>(SIGHASH_NONE), std::string("NONE"))
    (static_cast<unsigned char>(SIGHASH_NONE|SIGHASH_ANYONECANPAY), std::string("NONE|ANYONECANPAY"))
    (static_cast<unsigned char>(SIGHASH_SINGLE), std::string("SINGLE"))
    (static_cast<unsigned char>(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY), std::string("SINGLE|ANYONECANPAY"))
    ;

/**
 * Create the assembly string representation of a CScript object.
 * @param[in] script    CScript object to convert into the asm string representation.
 * @param[in] fAttemptSighashDecode    Whether to attempt to decode sighash types on data within the script that matches the format
 *                                     of a signature. Only pass true for scripts you believe could contain signatures. For example,
 *                                     pass false, or omit the this argument (defaults to false), for scriptPubKeys.
 */
std::string ScriptToAsmStr(const CScript& script, const bool fAttemptSighashDecode)
{
    std::string str;
    opcodetype opcode;
    std::vector<unsigned char> vch;
    CScript::const_iterator pc = script.begin();
    while (pc < script.end()) {
        if (!str.empty()) {
            str += " ";
        }
        if (!script.GetOp(pc, opcode, vch)) {
            str += "[error]";
            return str;
        }
        if (0 <= opcode && opcode <= OP_PUSHDATA4) {
            if (vch.size() <= static_cast<std::vector<unsigned char>::size_type>(4)) {
                str += strprintf("%d", CScriptNum(vch, false).getint());
            } else {
                // the IsUnspendable check makes sure not to try to decode OP_RETURN data that may match the format of a signature
                if (fAttemptSighashDecode && !script.IsUnspendable()) {
                    std::string strSigHashDecode;
                    // goal: only attempt to decode a defined sighash type from data that looks like a signature within a scriptSig.
                    // this won't decode correctly formatted public keys in Pubkey or Multisig scripts due to
                    // the restrictions on the pubkey formats (see IsCompressedOrUncompressedPubKey) being incongruous with the
                    // checks in CheckSignatureEncoding.
                    uint32_t flags = SCRIPT_VERIFY_STRICTENC;
                    if (vch.back() & SIGHASH_FORKID) {
                        // If the transaction is using SIGHASH_FORKID, we need
                        // to set the apropriate flag.
                        // TODO: Remove after the Hard Fork.
                        flags |= SCRIPT_ENABLE_SIGHASH_FORKID;
                    }
                    Script::State state(flags);
                    if (Script::checkTransactionSignatureEncoding(vch, state)) {
                        const uint8_t chSigHashType = vch.back() &  0xBF;
                        if (mapSigHashTypes.count(chSigHashType)) {
                            const bool forkIdSet = (vch.back() & SIGHASH_FORKID) == SIGHASH_FORKID;
                            strSigHashDecode = "[" + mapSigHashTypes.find(chSigHashType)->second
                                    + (forkIdSet ? "|FORKID]" : "]");
                            vch.pop_back(); // remove the sighash type byte. it will be replaced by the decode.
                        }
                    }
                    str += HexStr(vch) + strSigHashDecode;
                } else {
                    str += HexStr(vch);
                }
            }
        } else {
            str += GetOpName(opcode);
        }
    }
    return str;
}

std::string EncodeHexTx(const CTransaction& tx)
{
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << tx;
    return HexStr(ssTx.begin(), ssTx.end());
}
