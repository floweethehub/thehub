/*
 * This file is part of the Flowee project
 * Copyright (C) 2018-2020 Tom Zander <tomz@freedommail.ch>
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
#include "TransactionMonitorService.h"

// server 'lib'
#include <txmempool.h>
#include <DoubleSpendProof.h>
#include <DoubleSpendProofStorage.h>
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

TransactionMonitorService::TransactionMonitorService()
    : NetworkService(Api::TransactionMonitorService)
{
    ValidationNotifier().addListener(this);
    
    logFatal(Log::MonitorService) << "Transaction monitor created.";
}

TransactionMonitorService::~TransactionMonitorService()
{
    ValidationNotifier().removeListener(this);
}

void TransactionMonitorService::SyncTx(const Tx &tx)
{
    if (!m_findByHash)
        return;
    auto txHash = tx.createHash();
    for (auto remote_ : remotes()) {
        auto remote = dynamic_cast<RemoteWithHashes*>(remote_);
        assert(remote);
        if (remote->hashes.find(txHash) != remote->hashes.end()) {
            remote->pool.reserve(75);
            Streaming::MessageBuilder builder(remote->pool);
            builder.add(Api::TxId, txHash);
            logDebug(Log::MonitorService) << "Remote gets tx notification for" << txHash;
            remote->connection.send(builder.message(Api::TransactionMonitorService, Api::TransactionMonitor::TransactionFound));
        }
    }
}

namespace  {
    struct Match {
        Match (int64_t oib, const uint256 &h) : offsetInBlock(oib), hash(h) {}
        uint64_t offsetInBlock = 0;
        uint256 hash;
    };
}

void TransactionMonitorService::SyncAllTransactionsInBlock(const FastBlock &block, CBlockIndex *index)
{
    if (!m_findByHash)
        return;

    Tx::Iterator iter(block);
    auto type = iter.next();
    assert(type != Tx::End); // empty block (not even coinbase) is invalid.

    auto remotes = this->remotes();
    std::vector<std::deque<Match> > matches;
    matches.resize(remotes.size());
    bool seenOneEnd = false;
    while (type != Tx::End) {
        if (type == Tx::End) {
            if (seenOneEnd)
                break; // block done.
            seenOneEnd = true;

            auto txId = iter.prevTx().createHash();
            for (size_t i = 0; i < remotes.size(); ++i) {
                auto remote = static_cast<RemoteWithHashes*>(remotes[i]);
                if (remote->hashes.find(txId) == remote->hashes.end()) {
                    matches[i].push_back({iter.prevTx().offsetInBlock(block), txId});
                }
            }
        }
        else {
            seenOneEnd = false;
        }
        type = iter.next();
    }

    for (size_t i = 0; i < matches.size(); ++i) {
        const std::deque<Match> matchesForRemote = matches[i];
        if (!matchesForRemote.empty()) {
            assert(remotes.size() > i);
            auto remote = remotes[i];
            remote->pool.reserve(matchesForRemote.size() * 35 + 20);
            Streaming::MessageBuilder builder(remote->pool);
            for (auto m : matchesForRemote) {
                builder.add(Api::TransactionMonitor::TxId, m.hash);
                builder.add(Api::TransactionMonitor::OffsetInBlock, m.offsetInBlock);
            }
            logDebug(Log::MonitorService) << "Remote" << i << "gets" << matchesForRemote.size() << "txid notification(s) from block";
            builder.add(Api::TransactionMonitor::BlockHeight, index->nHeight);
            remote->connection.send(builder.message(Api::TransactionMonitorService, Api::TransactionMonitor::TransactionFound));
        }
    }
}

void TransactionMonitorService::DoubleSpendFound(const Tx &first, const Tx &duplicate)
{
#if 0
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
        m_pool.reserve(match.hashes.size() * 35 + 30 + duplicate.size());
        Streaming::MessageBuilder builder(m_pool);
        for (auto hash : match.hashes)
            builder.add(Api::TransactionMonitor::TxId, hash);
        builder.add(Api::AddressMonitor::TxId, first.createHash());
        builder.add(Api::AddressMonitor::GenericByteData, duplicate.data());
        rem[i->first]->connection.send(builder.message(Api::TransactionMonitorService, Api::TransactionMonitor::DoubleSpendFound));
    }
#endif
}

void TransactionMonitorService::DoubleSpendFound(const Tx &txInMempool, const DoubleSpendProof &proof)
{
#if 0
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
        m_pool.reserve(match.hashes.size() * 35 + 35 + serializedProof.size());
        Streaming::MessageBuilder builder(m_pool);
        for (auto hash : match.hashes)
            builder.add(Api::AddressMonitor::BitcoinScriptHashed, hash);
        builder.add(Api::AddressMonitor::TxId, txInMempool.createHash());
        builder.addByteArray(Api::AddressMonitor::GenericByteData, &serializedProof[0], serializedProof.size());
        rem[i->first]->connection.send(builder.message(Api::TransactionMonitorService, Api::AddressMonitor::DoubleSpendFound));
    }
#endif
}

void TransactionMonitorService::onIncomingMessage(Remote *remote_, const Message &message, const EndPoint &ep)
{
logFatal(Log::MonitorService) << "Subscribe message received.";
  
    assert(dynamic_cast<RemoteWithHashes*>(remote_));
    RemoteWithHashes *remote = static_cast<RemoteWithHashes*>(remote_);

    if (message.messageId() == Api::TransactionMonitor::Subscribe
            || message.messageId() == Api::TransactionMonitor::Unsubscribe) {
        Streaming::MessageParser parser(message.body());

        std::string error;
        int done = 0;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::TransactionMonitor::TxId) {
                if (parser.isByteArray() && parser.dataLength() == 32) {
                    uint256 hash = parser.uint256Data();

                    ++done;
                    if (message.messageId() == Api::TransactionMonitor::Subscribe) {
                        remote->hashes.insert(hash);
                        remote->connection.postOnStrand(std::bind(&TransactionMonitorService::findTxInMempool,
                                                                  this, remote->connection.connectionId(), hash));
                    } else {
                        remote->hashes.erase(hash);
                    }
                }
                else {
                    error = "TxId must be a bytearray of 32 bytes";
                }
            }
        }
        if (!done)
            error = "Missing required field TxId (4)";

        remote->pool.reserve(10 + error.size());
        Streaming::MessageBuilder builder(remote->pool);
        builder.add(Api::TransactionMonitor::Result, done);
        if (message.messageId() == Api::TransactionMonitor::Subscribe)
            logInfo(Log::MonitorService) << "Remote" << ep.connectionId << "registered" << done << "new TxId's";
        if (!error.empty())
            builder.add(Api::TransactionMonitor::ErrorMessage, error);
        remote->connection.send(builder.reply(message, Api::TransactionMonitor::SubscribeReply));
        updateBools();
    }
}


void TransactionMonitorService::updateBools()
{
    m_findByHash = false;
    for (auto remote : remotes()) {
        RemoteWithHashes *rwk = static_cast<RemoteWithHashes*>(remote);
        m_findByHash = m_findByHash || !rwk->hashes.empty();
    }
}

void TransactionMonitorService::findTxInMempool(int connectionId, const uint256 &hash)
{
    if (m_mempool == nullptr)
        return;
    if (manager() == nullptr)
        return;

    auto connection = manager()->connection(manager()->endPoint(connectionId), NetworkManager::OnlyExisting);
    if (!connection.isValid() || !connection.isConnected())
        return;

    Tx tx;
    if (m_mempool->lookup(hash, tx)) {
        std::lock_guard<std::mutex> guard(m_poolMutex);
        m_pool.reserve(75);
        Streaming::MessageBuilder builder(m_pool);
        builder.add(Api::TransactionMonitor::TxId, hash);
        Message message = builder.message(Api::TransactionMonitorService, Api::TransactionMonitor::TransactionFound);
        connection.send(message);
    }
    else {
        return;
    }

    DoubleSpendProof dsp;
    if (m_mempool->doubleSpendProofFor(hash, dsp)) {
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << dsp;
        const std::vector<uint8_t> serializedProof(stream.begin(), stream.end());

        std::lock_guard<std::mutex> guard(m_poolMutex);
        m_pool.reserve(50 + serializedProof.size());
        Streaming::MessageBuilder builder(m_pool);
        builder.add(Api::TransactionMonitor::TxId, hash);
        builder.addByteArray(Api::AddressMonitor::GenericByteData, &serializedProof[0], serializedProof.size());
        connection.send(builder.message(Api::TransactionMonitorService, Api::AddressMonitor::DoubleSpendFound));
    }
}
