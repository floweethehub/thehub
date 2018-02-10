/*
 * This file is part of the Flowee project
// Copyright (c) 2009-2015 The Bitcoin Core developers
 * Copyright (C) 2017 Tom Zander <tomz@freedommail.ch>
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

#include "base58.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/standard.h"
#include "serialize.h"
#include "streams.h"
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
                    if (CheckSignatureEncoding(vch, flags, NULL)) {
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

void ScriptPubKeyToUniv(const CScript& scriptPubKey,
                        UniValue& out, bool fIncludeHex)
{
    txnouttype type;
    std::vector<CTxDestination> addresses;
    int nRequired;

    out.pushKV("asm", ScriptToAsmStr(scriptPubKey));
    if (fIncludeHex)
        out.pushKV("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
        out.pushKV("type", GetTxnOutputType(type));
        return;
    }

    out.pushKV("reqSigs", nRequired);
    out.pushKV("type", GetTxnOutputType(type));

    UniValue a(UniValue::VARR);
    BOOST_FOREACH(const CTxDestination& addr, addresses)
        a.push_back(CBitcoinAddress(addr).ToString());
    out.pushKV("addresses", a);
}

void TxToUniv(const CTransaction& tx, const uint256& hashBlock, UniValue& entry)
{
    entry.pushKV("txid", tx.GetHash().GetHex());
    entry.pushKV("version", tx.nVersion);
    entry.pushKV("locktime", (int64_t)tx.nLockTime);

    UniValue vin(UniValue::VARR);
    BOOST_FOREACH(const CTxIn& txin, tx.vin) {
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.pushKV("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
        else {
            in.pushKV("txid", txin.prevout.hash.GetHex());
            in.pushKV("vout", (int64_t)txin.prevout.n);
            UniValue o(UniValue::VOBJ);
            o.pushKV("asm", ScriptToAsmStr(txin.scriptSig, true));
            o.pushKV("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
            in.pushKV("scriptSig", o);
        }
        in.pushKV("sequence", (int64_t)txin.nSequence);
        vin.push_back(in);
    }
    entry.pushKV("vin", vin);

    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& txout = tx.vout[i];

        UniValue out(UniValue::VOBJ);

        UniValue outValue(UniValue::VNUM, FormatMoney(txout.nValue));
        out.pushKV("value", outValue);
        out.pushKV("n", (int64_t)i);

        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToUniv(txout.scriptPubKey, o, true);
        out.pushKV("scriptPubKey", o);
        vout.push_back(out);
    }
    entry.pushKV("vout", vout);

    if (!hashBlock.IsNull())
        entry.pushKV("blockhash", hashBlock.GetHex());

    entry.pushKV("hex", EncodeHexTx(tx)); // the hex-encoded transaction. used the name "hex" to be consistent with the verbose output of "getrawtransaction".
}
