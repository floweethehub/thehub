/*
 * This file is part of the Flowee project
 * Copyright (C) 2013 The Bitcoin Core developers
 * Copyright (C) 2016 Tom Zander <tom@flowee.org>
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
#include "transaction_utils.h"
#include <primitives/transaction.h>
#include <random.h>
#include <utilstrencodings.h>

#include <boost/atomic.hpp>

void TxUtils::RandomScript(CScript &script) {
    static const opcodetype oplist[] = {OP_FALSE, OP_1, OP_2, OP_3, OP_CHECKSIG, OP_IF, OP_VERIF, OP_RETURN, OP_CODESEPARATOR};
    script = CScript();
    int ops = (insecure_rand() % 10);
    for (int i=-5; i<ops; i++)
        script << oplist[insecure_rand() % (sizeof(oplist)/sizeof(oplist[0]))];
}

void TxUtils::RandomInScript(CScript &script) {
    script = CScript();
    int ops = 1 + (insecure_rand() % 5);
    for (int i = 0; i < ops; ++i) {
        int type = (insecure_rand() % 10);
        if (type == 0) {
            script << OP_FALSE;
        } else {
            std::vector<unsigned char> signature;
            const int length = (type == 1 ? 50 : 1) + insecure_rand() % 100;
            signature.reserve(length);
            GetRandBytes(&signature[0], length);
            script << signature;
        }
    }
}

void TxUtils::RandomTransaction(CMutableTransaction &tx, RandomTransactionType single) {
    tx.nVersion = 1;
    tx.vin.clear();
    tx.vout.clear();
    tx.nLockTime = (insecure_rand() % 2) ? insecure_rand() : 0;
    int ins = (insecure_rand() % 4) + 1;
    int outs = single == SingleOutput ? 1 : (insecure_rand() % 4) + 1;
    for (int in = 0; in < ins; in++) {
        tx.vin.push_back(CTxIn());
        CTxIn &txin = tx.vin.back();
        txin.prevout.hash = GetRandHash();
        txin.prevout.n = insecure_rand() % 4;
        RandomInScript(txin.scriptSig);
        txin.nSequence = (unsigned int)-1;
    }
    for (int out = 0; out < outs; out++) {
        tx.vout.push_back(CTxOut());
        CTxOut &txout = tx.vout.back();
        txout.nValue = insecure_rand() % 100000000;
        RandomScript(txout.scriptPubKey);
    }
}

std::vector<CTransaction> TxUtils::transactionsForBlock(int minSize)
{
    CMutableTransaction mtx;
    TxUtils::RandomTransaction(mtx, TxUtils::AnyOutputCount);
    for (size_t i = 0; i < mtx.vin.size(); ++i) { // make sure we can actually validate it without the 'non-final' warning.
        mtx.vin[i].nSequence = CTxIn::SEQUENCE_FINAL;
    }
    const CTransaction tx(mtx);
    const int count = minSize / tx.GetSerializeSize(0, 0) + 1;

    std::vector<CTransaction> answer;
    answer.reserve(count);
    for (int i = 0; i < count; ++i) {
        answer.push_back(tx);
    }
    return std::move(answer);
}

std::string TxUtils::FormatScript(const CScript& script)
{
    std::string ret;
    CScript::const_iterator it = script.begin();
    opcodetype op;
    while (it != script.end()) {
        CScript::const_iterator it2 = it;
        std::vector<unsigned char> vch;
        if (script.GetOp2(it, op, &vch)) {
            if (op == OP_0) {
                ret += "0 ";
                continue;
            } else if ((op >= OP_1 && op <= OP_16) || op == OP_1NEGATE) {
                ret += strprintf("%i ", op - OP_1NEGATE - 1);
                continue;
            } else if (op >= OP_NOP && op <= OP_CHECKMULTISIGVERIFY) {
                std::string str(GetOpName(op));
                if (str.substr(0, 3) == std::string("OP_")) {
                    ret += str.substr(3, std::string::npos) + " ";
                    continue;
                }
            }
            if (vch.size() > 0) {
                ret += strprintf("0x%x 0x%x ", HexStr(it2, it - vch.size()), HexStr(it - vch.size(), it));
            } else {
                ret += strprintf("0x%x", HexStr(it2, it));
            }
            continue;
        }
        ret += strprintf("0x%x ", HexStr(it2, script.end()));
        break;
    }
    return ret.substr(0, ret.size() - 1);
}
