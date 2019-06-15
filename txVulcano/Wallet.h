/*
 * This file is part of the Flowee project
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
#ifndef WALLET_H
#define WALLET_H

#include <map>

/*
 * it stores private keys
   I guess encryption of this may be nice.
 * it stores public keys
 * it stores transactions applicable to the keys it owns.
   store by block-height and offset-in-block
 * it stores the last block it saw (block-id)
 * I should also be able to fund a Transaction Creator, which selects from
   funds and then signs.

 when starting I connect it and it starts by checking which blocks it has not
 seen yet and download those and find any transactions in them.

 I need some way to get a list of unspend outputs
*/

#include <boost/filesystem/path.hpp>

#include <primitives/key.h>
#include <primitives/pubkey.h>

#include <primitives/FastTransaction.h>

class TransactionBuilder;

class Wallet
{
public:
    Wallet(const boost::filesystem::path &dbFile);
    ~Wallet();

    void addKey(const CKey &key, int blockheight = -1);
    void addOutput(int blockHeight, const uint256 &txid, int offsetInBlock, int outIndex, int64_t amount, const CKeyID &destAddress, const CScript &script);
    void addOutput(const uint256 &txid, int outIndex, int64_t amount, int keyId, short unconfirmedDepth, const CScript &script);
    void clearUnconfirmedUTXOs();

    int keyCount() const {
        return static_cast<int>(m_keys.size());
    }

    const CKey *privateKey(int keyId) const;

    int firstEmptyPubKey() const;
    std::vector<int> publicKeys() const;
    const CPubKey &publicKey(int id) const;

    void saveKeys();
    void saveCache();

    uint256 lastCachedBlock() const;
    void setLastCachedBlock(const uint256 &lastCachedBlock);

    struct UnspentOutput {
        uint256 prevTxId;
        short index, keyId, unconfirmedDepth;
        int coinbaseHeight;
        int64_t amount;
        CScript prevOutScript;
    };
    const std::list<UnspentOutput> &unspentOutputs() const;
    std::list<UnspentOutput>::const_iterator spendOutput(const std::list<UnspentOutput>::const_iterator &output);

private:
    const boost::filesystem::path m_dbFile;
    void loadKeys();

    std::list<std::pair<int, CKey> > m_keys; // private keys.

    struct WalletPubKey {
        WalletPubKey(const CPubKey &pk) : pubKey(pk), bitcoinAddress(pk.GetID()) {}
        CPubKey pubKey;
        CKeyID bitcoinAddress;

        // cache
        int64_t value = 0;
    };
    std::map<short, WalletPubKey> m_pubkeys;

    struct ValueTransfer {
        ValueTransfer(int keyId, int64_t amount) : keyId(keyId), amount(amount) {}
        int keyId = -1;
        int64_t amount = 0;
    };

    struct WalletItem { // aka a tx that touches one of our private-keys.
        int blockHeight = -1;
        uint32_t byteOffsetInblock = 0;

        // cache
        uint256 txid;
        std::vector<ValueTransfer> valueTransfer;
    };

    std::list<WalletItem> m_walletItems;

    // cache
    std::list<UnspentOutput> m_unspentOutputs;

    uint256 m_lastCachedBlock;

    bool m_privKeysNeedsSave = false;
};

#endif
