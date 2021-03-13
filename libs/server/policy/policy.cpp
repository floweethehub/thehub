/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2015 The Bitcoin developers
 * Copyright (C) 2019-2021 Tom Zander <tom@flowee.org>
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

#include "policy/policy.h"

#include <SettingsDefaults.h>
#include "main.h"
#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"
#include <primitives/FastTransaction.h>

#include <utxo/UnspentOutputDatabase.h>
#include <UnspentOutputData.h>

#include <cmath>
#include <algorithm>

    /**
     * Check transaction inputs to mitigate two
     * potential denial-of-service attacks:
     * 
     * 1. scriptSigs with extra data stuffed into them,
     *    not consumed by scriptPubKey (or P2SH script)
     * 2. P2SH scripts with a crazy number of expensive
     *    CHECKSIG/CHECKMULTISIG operations
     *
     * Check transaction inputs, and make sure any
     * pay-to-script-hash transactions are evaluating IsStandard scripts
     * 
     * Why bother? To avoid denial-of-service attacks; an attacker
     * can submit a standard HASH... OP_EQUAL transaction,
     * which will get accepted into blocks. The redemption
     * script can be anything; an attacker could use a very
     * expensive-to-check-upon-redemption script like:
     *   DUP CHECKSIG DROP ... repeated 100 times... OP_1
     */

bool IsStandard(const CScript &scriptPubKey, Script::TxnOutType &whichType, int &dataUsed)
{
    std::vector<std::vector<unsigned char> > vSolutions;
    if (!Script::solver(scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == Script::TX_MULTISIG) {
        unsigned char m = vSolutions.front()[0];
        unsigned char n = vSolutions.back()[0];
        // Support up to x-of-3 multisig txns as standard
        if (n < 1 || n > 3)
            return false;
        if (m < 1 || m > n)
            return false;
    } else if (whichType == Script::TX_NULL_DATA) {
        if (!fAcceptDatacarrier)
            return false;
        dataUsed += scriptPubKey.size() - 3; // (-1 for OP_RETURN, -2 for the pushdata opcodes)
    }

    return whichType != Script::TX_NONSTANDARD;
}

bool IsStandardTx(const CTransaction& tx, std::string& reason)
{
    if (tx.nVersion > CTransaction::MAX_STANDARD_VERSION || tx.nVersion < 1) {
        reason = "version";
        return false;
    }

    // Extremely large transactions with lots of inputs can cost the network
    // almost as much to process as they cost the sender in fees, because
    // computing signature hashes is O(ninputs*txsize). Limiting transactions
    // to MAX_STANDARD_TX_SIZE mitigates CPU exhaustion attacks.
    size_t sz = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);
    if (sz > MAX_STANDARD_TX_SIZE) {
        reason = "tx-size";
        return false;
    }

    for (const CTxIn& txin : tx.vin) {
        // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
        // keys. (remember the 520 byte limit on redeemScript size) That works
        // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
        // bytes of scriptSig, which we round off to 1650 bytes for some minor
        // future-proofing. That's also enough to spend a 20-of-20
        // CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not
        // considered standard)
        if (txin.scriptSig.size() > 1650) {
            reason = "scriptsig-size";
            return false;
        }
        if (!txin.scriptSig.IsPushOnly()) {
            reason = "scriptsig-not-pushonly";
            return false;
        }
    }

    Script::TxnOutType whichType;
    int dataUsed = 0; // TX_NULL_DATA type number of bytes used
    for (const CTxOut& txout : tx.vout) {
        if (!::IsStandard(txout.scriptPubKey, whichType, dataUsed)) {
            reason = "scriptpubkey";
            return false;
        } else if (whichType == Script::TX_NULL_DATA && dataUsed > nMaxDatacarrierBytes) {
            reason = "oversize-op-return";
            return false;
        } else if ((whichType == Script::TX_MULTISIG) && (!fIsBareMultisigStd)) {
            reason = "bare-multisig";
            return false;
        } else if (txout.IsDust(::minRelayTxFee)) {
            reason = "dust";
            return false;
        }
    }

    return true;
}

bool Policy::isInputStandard(const CScript &outputScript, const CScript &inputScript)
{
    std::vector<std::vector<unsigned char> > vSolutions;
    Script::TxnOutType whichType;
    if (!Script::solver(outputScript, whichType, vSolutions))
        return false;
    if (whichType == Script::TX_SCRIPTHASH) {
        std::vector<std::vector<unsigned char> > stack;
        // convert the scriptSig into a stack, so we can inspect the redeemScript
        Script::State state;
        if (!Script::eval(stack, inputScript, BaseSignatureChecker(), state))
            return false;
        if (stack.empty())
            return false;
    }

    return true;
}

int32_t Policy::blockSizeAcceptLimit()
{
    int64_t limit = -1;
    auto userlimit = mapArgs.find("-blocksizeacceptlimit");
    if (userlimit == mapArgs.end()) {
        limit = GetArg("-blocksizeacceptlimitbytes", -1);
        if (limit == -1) // fallback to the BitcoinUnlimited name.
           limit = GetArg("-excessiveblocksize", -1);
    }
    else {
        // this is in fractions of a megabyte (for instance "3.2")
        double limitInMB = atof(userlimit->second.c_str());
        if (limitInMB <= 0) {
            LogPrintf("Failed to understand blocksizeacceptlimit: '%s'\n", userlimit->second.c_str());
        } else {
            limit = static_cast<int64_t>(round(limitInMB * 1000000));
            limit -= (limit % 100000); // only one digit behind the dot was allowed
        }
    }
    if (limit <= 0)
        limit = Settings::DefaultBlockAcceptSize;
    if (limit < 1000000)
        logCritical(Log::Bitcoin).nospace() << "BlockSize set to extremely low value (" << limit << " bytes), this may cause failures.";
    return static_cast<int>(std::min(int64_t(INT_MAX), limit));
}

bool Policy::areInputsStandard(const Tx &tx, const UnspentOutputDatabase *utxo)
{
    Tx::Iterator iter(tx);
    auto type = iter.next(Tx::PrevTxHash);
    while (type != Tx::End) {
        uint256 prevTxHash = iter.uint256Data();
        type = iter.next();
        if (type != Tx::PrevTxIndex)
            return false;
        UnspentOutputData data(utxo->find(prevTxHash, iter.intData()));
        if (!data.isValid())
            return false;
        type = iter.next();
        if (type != Tx::TxInScript)
            return false;

        const bool isStandardInput = isInputStandard(data.outputScript(), iter.byteData());
        if (!isStandardInput)
            return false;
        type = iter.next(Tx::PrevTxHash);
    }
    return true;
}

uint32_t Policy::blockSigCheckAcceptLimit()
{
    return (blockSizeAcceptLimit() + 70) / 141;
}
