/*
 * This file is part of the Flowee project
 * Copyright (C) 2018-2019 Tom Zander <tomz@freedommail.ch>
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
#include "AddressMonitorService.h"
#include <encodings_legacy.h>
#include <NetworkManager.h>
#include <APIProtocol.h>

#include <Logger.h>
#include <Message.h>
#include <chain.h>

#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

#include "Application.h"
#include "txmempool.h"

#include <primitives/FastBlock.h>

AddressMonitorService::AddressMonitorService()
    : NetworkService(Api::AddressMonitorService)
{
    ValidationNotifier().addListener(this);
}

AddressMonitorService::~AddressMonitorService()
{
    ValidationNotifier().removeListener(this);
}

void AddressMonitorService::SyncTx(const Tx &tx)
{
    const auto rem = remotes();
    std::map<int, Match> matches;
    Tx::Iterator iter(tx);
    bool m = match(iter, rem, matches);
    if (!m)
        return;

    for (auto i = matches.begin(); i != matches.end(); ++i) {
        Match &match = i->second;
        std::lock_guard<std::mutex> guard(m_poolMutex);
        m_pool.reserve(match.keys.size() * 24 + 50);
        Streaming::MessageBuilder builder(m_pool);
        for (auto key : match.keys)
            builder.add(Api::AddressMonitor::BitcoinAddress, key);
        builder.add(Api::AddressMonitor::Amount, i->second.amount);
        builder.add(Api::AddressMonitor::TxId, tx.createHash());
        rem[i->first]->connection.send(builder.message(Api::AddressMonitorService, Api::AddressMonitor::TransactionFound));
    }
}

bool AddressMonitorService::match(Tx::Iterator &iter, const std::deque<NetworkService::Remote *> &remotes, std::map<int, Match> &matchingRemotes) const
{
    if (remotes.empty())
        return false;
    auto type = iter.next();
    if (type == Tx::End) // then the second end means end of block
        return false;

    uint64_t amount = 0;
    while (type != Tx::End) {
        if (type == Tx::OutputValue)
            amount = iter.longData();
        else if (type == Tx::OutputScript) {
            CScript scriptPubKey(iter.byteData());

            std::vector<std::vector<unsigned char> > vSolutions;
            Script::TxnOutType whichType;
            bool recognizedTx = Script::solver(scriptPubKey, whichType, vSolutions);
            if (recognizedTx && whichType != Script::TX_NULL_DATA) {
                if (m_findP2PKH && (whichType == Script::TX_PUBKEY || whichType == Script::TX_PUBKEYHASH)) {
                    CKeyID keyID;
                    if (whichType == Script::TX_PUBKEY)
                        keyID = CPubKey(vSolutions[0]).GetID();
                    else if (whichType == Script::TX_PUBKEYHASH)
                        keyID = CKeyID(uint160(vSolutions[0]));
                    for (size_t i = 0; i < remotes.size(); ++i) {
                        RemoteWithKeys *rwk = static_cast<RemoteWithKeys*>(remotes.at(i));
                        if (rwk->keys.find(keyID) != rwk->keys.end()) {
                            Match &m = matchingRemotes[i];
                            m.amount += amount;
                            m.keys.push_back(keyID);
                        }
                    }
                }
            }
        }
        type = iter.next();
    }
    return true;
}

void AddressMonitorService::SyncAllTransactionsInBlock(const FastBlock &block, CBlockIndex *index)
{
    assert(index);
    Tx::Iterator iter(block);
    auto rem = remotes();
    while (true) {
        std::map<int, Match> matches;
        if (!match(iter, rem, matches))
            break;
        for (auto i = matches.begin(); i != matches.end(); ++i) {
            Match &match = i->second;
            std::lock_guard<std::mutex> guard(m_poolMutex);
            m_pool.reserve(match.keys.size() * 24 + 30);
            Streaming::MessageBuilder builder(m_pool);
            for (auto key : match.keys)
                builder.add(Api::AddressMonitor::BitcoinAddress, key);
            builder.add(Api::AddressMonitor::Amount, i->second.amount);
            builder.add(Api::AddressMonitor::OffsetInBlock, static_cast<uint64_t>(iter.prevTx().offsetInBlock(block)));
            builder.add(Api::AddressMonitor::BlockHeight, index->nHeight);
            rem[i->first]->connection.send(builder.message(Api::AddressMonitorService, Api::AddressMonitor::TransactionFound));
        }
    }
}

void AddressMonitorService::DoubleSpendFound(const Tx &first, const Tx &duplicate)
{
    logCritical(Log::MonitorService) << "Double spend found" << first.createHash() << duplicate.createHash();
    const auto rem = remotes();
    std::map<int, Match> matches;
    Tx::Iterator iter(first);
    bool m = match(iter, rem, matches);
    if (!m)
        return;

    Tx::Iterator iter2(duplicate);
    m = match(iter2, rem, matches);
    assert(m); // our duplicate tx object should have data

    for (auto i = matches.begin(); i != matches.end(); ++i) {
        Match &match = i->second;
        std::lock_guard<std::mutex> guard(m_poolMutex);
        m_pool.reserve(match.keys.size() * 24 + 40 + duplicate.size());
        Streaming::MessageBuilder builder(m_pool);
        for (auto key : match.keys)
            builder.add(Api::AddressMonitor::BitcoinAddress, key);
        builder.add(Api::AddressMonitor::Amount, i->second.amount);
        builder.add(Api::AddressMonitor::TxId, first.createHash());
        builder.add(Api::AddressMonitor::GenericByteData, duplicate.data());
        rem[i->first]->connection.send(builder.message(Api::AddressMonitorService, Api::AddressMonitor::DoubleSpendFound));
    }
}

void AddressMonitorService::onIncomingMessage(Remote *remote_, const Message &message, const EndPoint &ep)
{
    assert(dynamic_cast<RemoteWithKeys*>(remote_));
    RemoteWithKeys *remote = static_cast<RemoteWithKeys*>(remote_);
    if (message.messageId() == Api::AddressMonitor::Subscribe)
        logInfo(Log::MonitorService) << "Remote" << ep.connectionId << "registered a new address";

    if (message.messageId() == Api::AddressMonitor::Subscribe
            || message.messageId() == Api::AddressMonitor::Unsubscribe) {
        Streaming::MessageParser parser(message.body());

        std::string error;
        int done = 0;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::AddressMonitor::BitcoinAddress) {
                ++done;
                if (parser.isByteArray() && parser.dataLength() == 20) {
                    CKeyID id(parser.bytesDataBuffer().begin());

                    if (message.messageId() == Api::AddressMonitor::Subscribe) {
                        remote->keys.insert(id);
                        remote->connection.postOnStrand(std::bind(&AddressMonitorService::findTxInMempool,
                                                                  this, remote->connection.connectionId(), id));
                    } else {
                        remote->keys.erase(id);
                    }
                }
                else {
                    error = "address has to be a bytearray of 20 bytes";
                }
            }
        }
        if (!done)
            error = "Missing required field BitcoinAddress (2)";

        remote->pool.reserve(10 + error.size());
        Streaming::MessageBuilder builder(remote->pool);
        builder.add(Api::AddressMonitor::Result, done);
        if (!error.empty())
            builder.add(Api::AddressMonitor::ErrorMessage, error);
        remote->connection.send(builder.reply(message));
        updateBools();
    }
}


void AddressMonitorService::updateBools()
{
    // ok, the first usage is a point-of-sale, I see no need to use P2SH or multisig, so we only actually
    // monitor P2PKH types for now... Boring, I know.
    m_findP2PKH = false;
    for (auto remote : remotes()) {
        RemoteWithKeys *rwk = static_cast<RemoteWithKeys*>(remote);
        m_findP2PKH = m_findP2PKH || !rwk->keys.empty();
    }
}

void AddressMonitorService::findTxInMempool(int connectionId, const CKeyID &keyId)
{
    if (m_mempool == nullptr)
        return;
    if (manager() == nullptr)
        return;

    auto connection = manager()->connection(manager()->endPoint(connectionId), NetworkManager::OnlyExisting);
    if (!connection.isValid() || !connection.isConnected())
        return;

    LOCK(m_mempool->cs);
    for (auto iter = m_mempool->mapTx.begin(); iter != m_mempool->mapTx.end(); ++iter) {
        Tx::Iterator txIter(iter->tx);
        auto type = txIter.next();
        uint64_t curAmount = 0, matchedAmounts = 0;
        bool match = false;
        while (true) {
            if (type == Tx::End) {
                if (match) {
                    logDebug(Log::MonitorService) << " + Sending to peers tx from mempool!";
                    m_pool.reserve(75);
                    Streaming::MessageBuilder builder(m_pool);
                    builder.add(Api::AddressMonitor::BitcoinAddress, CBitcoinAddress(keyId).ToString());
                    builder.add(Api::AddressMonitor::TxId, txIter.prevTx().createHash());
                    builder.add(Api::AddressMonitor::Amount, matchedAmounts);
                    Message message = builder.message(Api::AddressMonitorService, Api::AddressMonitor::TransactionFound);
                    connection.send(message);
                }
                break;
            }
            if (type == Tx::OutputValue)
                curAmount = txIter.longData();
            else if (type == Tx::OutputScript) {
                CScript scriptPubKey(txIter.byteData());
                std::vector<std::vector<unsigned char> > vSolutions;
                Script::TxnOutType whichType;
                bool recognizedTx = Script::solver(scriptPubKey, whichType, vSolutions);
                if (recognizedTx && whichType != Script::TX_NULL_DATA) {
                    if (whichType == Script::TX_PUBKEY || whichType == Script::TX_PUBKEYHASH) {
                        if ((whichType == Script::TX_PUBKEY && keyId == CPubKey(vSolutions[0]).GetID())
                                || (whichType == Script::TX_PUBKEYHASH && keyId == CKeyID(uint160(vSolutions[0])))) {

                            match = true;
                            matchedAmounts += curAmount;
                        }
                    }
                }
            }
            type = txIter.next();
        }
    }
}
