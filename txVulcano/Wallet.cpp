/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tom@flowee.org>
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
#include "Wallet.h"
#include "Wallet_p.h"

#include <boost/filesystem.hpp>

#include <streaming/MessageParser.h>
#include <streaming/BufferPool.h>
#include <streaming/MessageBuilder.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

using namespace Streaming;

Wallet::Wallet(const QString &dbFile)
    : m_dbFile(dbFile)
{
    loadKeys();
}

Wallet::~Wallet()
{
    if (m_privKeysNeedsSave) {
        saveKeys();
    }
}

void Wallet::addKey(const CKey &key, int blockheight)
{
    Q_UNUSED(blockheight);
    assert(key.IsValid());
    int id = m_keys.empty() ? 0 : (m_keys.back().first + 1);
    m_keys.push_back(std::make_pair(id, key));
    m_pubkeys.insert(std::make_pair(id, WalletPubKey(key.GetPubKey())));
    m_privKeysNeedsSave = true;
}

void Wallet::addOutput(int blockHeight, const uint256 &txid, int offsetInBlock, int outIndex, int64_t amount, const CKeyID &destAddress, const CScript &script)
{
    assert(outIndex < 0xFFFF);
    assert(amount >= 0);
    short keyId = -1;
    for (auto pk : m_pubkeys) {
        if (pk.second.bitcoinAddress == destAddress) {
            keyId = pk.first;
            pk.second.value += amount;
            break;
        }
    }
    if (keyId == -1)
        return;

    const int coinbaseHeight = offsetInBlock <= 91 ? blockHeight : -1;
    m_unspentOutputs.push_back({txid, (short) outIndex, keyId, 1, coinbaseHeight, amount, script});

    /*
    for (const WalletItem &wi : m_walletItems) {
        if (wi.blockHeight == blockHeight && offsetInBlock == wi.byteOffsetInblock && wi.txid ==  txid) {
            wi.valueTransfer.push_back(ValueTransfer(keyId, amount));
            return;
        }
    }
    WalletItem item;
    item.blockHeight = blockHeight;
    item.byteOffsetInblock = offsetInBlock;
    item.txid = txid;
    item.valueTransfer.push_back(ValueTransfer(keyId, amount));
    m_walletItems.push_back(item);
    */
}

void Wallet::addOutput(const uint256 &txid, int outIndex, int64_t amount, int keyId, short unconfirmedDepth, const CScript &script)
{
    // unconfirmed.
    assert(outIndex < 0xFFFF);
    assert(keyId < 0xFFFF);
    m_unspentOutputs.push_back({txid, (short) outIndex, (short) keyId, unconfirmedDepth, -2, amount, script});
}

void Wallet::clearUnconfirmedUTXOs()
{
    for (auto iter = m_unspentOutputs.begin(); iter != m_unspentOutputs.end();) {
        if (iter->coinbaseHeight == -2)
            iter = m_unspentOutputs.erase(iter);
        else
            ++iter;
    }
}

const CKey *Wallet::privateKey(int keyId) const
{
    for (auto iter = m_keys.begin(); iter != m_keys.end(); ++iter) {
        if (iter->first == keyId)
            return &iter->second;
    }
    return nullptr;
}

int Wallet::firstEmptyPubKey() const
{
    for (auto item : m_pubkeys) {
        if (item.second.value == 0)
            return item.first;
    }
    return -1;
}

const CPubKey &Wallet::publicKey(int id) const
{
    auto iter = m_pubkeys.find(id);
    assert(m_pubkeys.end() != iter);
    return iter->second.pubKey;
}

std::vector<int> Wallet::publicKeys() const
{
    std::vector<int> answer;
    answer.reserve(m_pubkeys.size());
    for (auto item : m_pubkeys) {
        answer.push_back(item.first);
    }
    return answer;
}

void Wallet::saveKeys()
{
    if (!m_privKeysNeedsSave)
        return;
    QFileInfo info(m_dbFile);
    if (!info.dir().exists()) {
        QDir root("/");
        root.mkpath(info.absolutePath());
    }

    BufferPool pool(m_keys.size() * 40);
    MessageBuilder builder(pool);
    for (auto &keyPair : m_keys) {
        const CKey &key = keyPair.second;
        assert(key.IsValid());
        builder.addByteArray(WalletPrivateKeys::PrivateKey, key.begin(), key.size());
    }
    builder.add(WalletPrivateKeys::End, true);
    QFile output(m_dbFile);
    if (output.open(QIODevice::WriteOnly)) {
        auto data = builder.buffer();
        output.write(data.begin(), data.size());
        output.close();
        m_privKeysNeedsSave = false;
    }
}

void Wallet::saveCache()
{
    // TODO
}

void Wallet::loadKeys()
{
    assert(m_keys.empty());
    QFile input(m_dbFile);
    if (!input.open(QIODevice::ReadOnly))
        return;
    auto fileSize = input.size();
    if (fileSize == -1) // no file to read
        return;
    if (fileSize < 0 || fileSize > 1E6) {
        logFatal() << "Input file is too large, refusing to load";
        throw std::runtime_error("Input file is too large");
    }
    BufferPool pool(fileSize);
    input.read(pool.begin(), fileSize);
    MessageParser parser(pool.commit(fileSize));
    Streaming::ParsedType type = parser.next();
    while (type == Streaming::FoundTag) {
        switch (static_cast<WalletPrivateKeys::WalletTokens>(parser.tag())) {
        case WalletPrivateKeys::PrivateKey:
            if (parser.dataLength() != 32) {
                logFatal()<< "Private key of wrong length" << parser.dataLength();
            } else {
                auto bytes = parser.unsignedBytesData();
                CKey key;
                key.Set(bytes.begin(), bytes.end(), true);
                if (key.IsValid())
                    addKey(key);
                else
                    logFatal() << "Failed to parse private key";
            }
            break;
        case WalletPrivateKeys::End: return;
        default:
            logFatal() << "Unknown tag encountered";
            break;
        }
        type = parser.next();
    }

    logDebug() << "Loading of private keys complete we now have: " << m_keys.size();
}

uint256 Wallet::lastCachedBlock() const
{
    return m_lastCachedBlock;
}

void Wallet::setLastCachedBlock(const uint256 &lastCachedBlock)
{
    m_lastCachedBlock = lastCachedBlock;
}

const std::list<Wallet::UnspentOutput> &Wallet::unspentOutputs() const
{
    return m_unspentOutputs;
}

std::list<Wallet::UnspentOutput>::const_iterator Wallet::spendOutput(const std::list<UnspentOutput>::const_iterator &iter)
{
    assert (iter != m_unspentOutputs.end());
    return m_unspentOutputs.erase(iter);
}
