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

// server 'lib'
#include <txmempool.h>
#include <DoubleSpendProof.h>
#include <encodings_legacy.h>
#include <chain.h>
#include <Application.h>

#include <NetworkManager.h>
#include <APIProtocol.h>

#include <Logger.h>
#include <Message.h>
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>
#include <streaming/streams.h>
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
    if (!match(iter, rem, matches))
        return;

    for (auto i = matches.begin(); i != matches.end(); ++i) {
        Match &match = i->second;
        std::lock_guard<std::mutex> guard(m_poolMutex);
        m_pool.reserve(match.hashes.size() * 35 + match.amounts.size() * 10 + 40);
        Streaming::MessageBuilder builder(m_pool);
        for (auto hash : match.hashes)
            builder.add(Api::AddressMonitor::BitcoinScriptHashed, hash);
        for (auto amount : match.amounts)
            builder.add(Api::AddressMonitor::Amount, amount);
        builder.add(Api::AddressMonitor::TxId, tx.createHash());
        logDebug(Log::MonitorService) << "Remote" << i->first << "gets" << match.hashes.size() << "tx notification(s)";
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
        if (type == Tx::OutputValue) {
            amount = iter.longData();
        }
        else if (type == Tx::OutputScript) {
            uint256 hashedOutScript;
            iter.hashByteData(hashedOutScript);
            for (size_t i = 0; i < remotes.size(); ++i) {
                assert(i < INT_MAX);
                RemoteWithKeys *rwk = static_cast<RemoteWithKeys*>(remotes.at(i));
                if (rwk->hashes.find(hashedOutScript) != rwk->hashes.end()) {
                    Match &m = matchingRemotes[static_cast<int>(i)];
                    m.amounts.push_back(amount);
                    m.hashes.push_back(hashedOutScript);
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
            m_pool.reserve(match.hashes.size() * 35 + match.amounts.size() * 10 + 20);
            Streaming::MessageBuilder builder(m_pool);
            for (auto hash : match.hashes)
                builder.add(Api::AddressMonitor::BitcoinScriptHashed, hash);
            for (auto amount : match.amounts)
                builder.add(Api::AddressMonitor::Amount, amount);
            builder.add(Api::AddressMonitor::OffsetInBlock, static_cast<uint64_t>(iter.prevTx().offsetInBlock(block)));
            builder.add(Api::AddressMonitor::BlockHeight, index->nHeight);
            logDebug(Log::MonitorService) << "Remote" << i->first << "gets" << match.hashes.size() << "tx notification(s) from block";
            rem[i->first]->connection.send(builder.message(Api::AddressMonitorService, Api::AddressMonitor::TransactionFound));
        }
    }
}

void AddressMonitorService::DoubleSpendFound(const Tx &first, const Tx &duplicate)
{
    logDebug(Log::MonitorService) << "Double spend found" << first.createHash() << duplicate.createHash();
    const auto rem = remotes();
    std::map<int, Match> matches;
    Tx::Iterator iter(first);
    if (!match(iter, rem, matches))
        return; // returns false if no listeners

    Tx::Iterator iter2(duplicate);
    bool m = match(iter2, rem, matches);
    assert(m); // our duplicate tx object should have data

    for (auto i = matches.begin(); i != matches.end(); ++i) {
        Match &match = i->second;
        std::lock_guard<std::mutex> guard(m_poolMutex);
        m_pool.reserve(match.hashes.size() * 35 + match.amounts.size() * 10 + 30 + duplicate.size());
        Streaming::MessageBuilder builder(m_pool);
        for (auto hash : match.hashes)
            builder.add(Api::AddressMonitor::BitcoinScriptHashed, hash);
        for (auto amount : match.amounts)
            builder.add(Api::AddressMonitor::Amount, amount);
        builder.add(Api::AddressMonitor::TxId, first.createHash());
        builder.add(Api::AddressMonitor::GenericByteData, duplicate.data());
        rem[i->first]->connection.send(builder.message(Api::AddressMonitorService, Api::AddressMonitor::DoubleSpendFound));
    }
}

void AddressMonitorService::DoubleSpendFound(const Tx &txInMempool, const DoubleSpendProof &proof)
{
    logDebug(Log::MonitorService) << "Double spend proof found. TxId:" << txInMempool.createHash();
    const auto rem = remotes();
    std::map<int, Match> matches;
    Tx::Iterator iter(txInMempool);
    if (!match(iter, rem, matches))
        return; // returns false if no listeners

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << proof;
    const std::vector<uint8_t> serializedProof(stream.begin(), stream.end());

    for (auto i = matches.begin(); i != matches.end(); ++i) {
        Match &match = i->second;
        std::lock_guard<std::mutex> guard(m_poolMutex);
        m_pool.reserve(match.hashes.size() * 35 + match.amounts.size() * 10 + 35 + serializedProof.size());
        Streaming::MessageBuilder builder(m_pool);
        for (auto hash : match.hashes)
            builder.add(Api::AddressMonitor::BitcoinScriptHashed, hash);
        for (auto amount : match.amounts)
            builder.add(Api::AddressMonitor::Amount, amount);
        builder.add(Api::AddressMonitor::TxId, txInMempool.createHash());
        builder.addByteArray(Api::AddressMonitor::GenericByteData, &serializedProof[0], serializedProof.size());
        rem[i->first]->connection.send(builder.message(Api::AddressMonitorService, Api::AddressMonitor::DoubleSpendFound));
    }
}

void AddressMonitorService::onIncomingMessage(Remote *remote_, const Message &message, const EndPoint &ep)
{
    assert(dynamic_cast<RemoteWithKeys*>(remote_));
    RemoteWithKeys *remote = static_cast<RemoteWithKeys*>(remote_);

    if (message.messageId() == Api::AddressMonitor::Subscribe
            || message.messageId() == Api::AddressMonitor::Unsubscribe) {
        Streaming::MessageParser parser(message.body());

        std::string error;
        int done = 0;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::AddressMonitor::BitcoinScriptHashed) {
                if (parser.isByteArray() && parser.dataLength() == 32) {
                    uint256 hash = parser.uint256Data();

                    ++done;
                    if (message.messageId() == Api::AddressMonitor::Subscribe) {
                        remote->hashes.insert(hash);
                        remote->connection.postOnStrand(std::bind(&AddressMonitorService::findTxInMempool,
                                                                  this, remote->connection.connectionId(), hash));
                    } else {
                        remote->hashes.erase(hash);
                    }
                }
                else {
                    error = "BitcoinScriptHashed has to be a sha256 (bytearray of 32 bytes)";
                }
            }
        }
        if (!done)
            error = "Missing required field BitcoinScriptHashed (2)";

        remote->pool.reserve(10 + error.size());
        Streaming::MessageBuilder builder(remote->pool);
        builder.add(Api::AddressMonitor::Result, done);
        if (message.messageId() == Api::AddressMonitor::Subscribe)
            logInfo(Log::MonitorService) << "Remote" << ep.connectionId << "registered" << done << "new script-hashes(es)";
        if (!error.empty())
            builder.add(Api::AddressMonitor::ErrorMessage, error);
        remote->connection.send(builder.reply(message));
        updateBools();
    }
}


void AddressMonitorService::updateBools()
{
    m_findByHash = false;
    for (auto remote : remotes()) {
        RemoteWithKeys *rwk = static_cast<RemoteWithKeys*>(remote);
        m_findByHash = m_findByHash || !rwk->hashes.empty();
    }
}

void AddressMonitorService::findTxInMempool(int connectionId, const uint256 &hash)
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
                    builder.add(Api::AddressMonitor::BitcoinScriptHashed, hash);
                    builder.add(Api::AddressMonitor::TxId, txIter.prevTx().createHash());
                    builder.add(Api::AddressMonitor::Amount, matchedAmounts);
                    Message message = builder.message(Api::AddressMonitorService, Api::AddressMonitor::TransactionFound);
                    connection.send(message);
                }
                break;
            }
            if (type == Tx::OutputValue) {
                curAmount = txIter.longData();
            }
            else if (type == Tx::OutputScript) {
                if (txIter.hashedByteData() == hash) {
                    match = true;
                    matchedAmounts += curAmount;
                }
            }
            type = txIter.next();
        }
    }
}
