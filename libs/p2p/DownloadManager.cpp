/*
 * This file is part of the Flowee project
 * Copyright (C) 2020 Tom Zander <tomz@freedommail.ch>
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
#include "DownloadManager.h"
#include "FillAddressDBAction.h"
#include "InventoryItem.h"
#include "P2PNetInterface.h"
#include "Peer.h"
#include "SyncChainAction.h"
#include "SyncSPVAction.h"

#include <primitives/FastTransaction.h>
#include <streaming/P2PParser.h>
#include <streaming/P2PBuilder.h>

DownloadManager::DownloadManager(boost::asio::io_service &service)
    : m_strand(service),
      m_connectionManager(service, this),
      m_blockchain(this),
      m_shuttingDown(false)
{
}

const ConnectionManager &DownloadManager::connectionManager() const
{
    return m_connectionManager;
}

ConnectionManager &DownloadManager::connectionManager()
{
    return m_connectionManager;
}

void DownloadManager::headersDownloadFinished(int newBlockHeight, int peerId)
{
    if (m_shuttingDown)
        return;
    assert(m_strand.running_in_this_thread());
    if (m_peerDownloadingHeaders == peerId)
        m_peerDownloadingHeaders = -1;

    auto peer = m_connectionManager.peer(peerId);
    if (peer.get())
        peer->peerAddress().gotGoodHeaders();

    if (m_peerDownloadingHeaders == -1) { // check if we need to download more of them.
        // TODO use the fastest peer.
        for (auto p : m_connectionManager.connectedPeers()) {
            if (p->startHeight() > newBlockHeight) {
                m_peerDownloadingHeaders = p->connectionId();
                auto p = m_connectionManager.peer(m_peerDownloadingHeaders);
                if (p) {
                    m_connectionManager.requestHeaders(p);
                    break;
                }
            }
        }
    }

    m_connectionManager.setBlockHeight(newBlockHeight);
    for (auto iface : m_listeners) {
        iface->blockchainHeightChanged(newBlockHeight);
    }

    addAction<SyncSPVAction>();
}

void DownloadManager::parseInvMessage(Message message, int sourcePeerId)
{
    if (m_shuttingDown)
        return;
    try {
        Streaming::P2PParser parser(message);
        const size_t count = parser.readCompactInt();
        logDebug() << "Received" << count << "Inv messages";
        std::unique_lock<std::mutex> lock(m_downloadsLock);
        for (size_t i = 0; i < count; ++i) {
            uint32_t type = parser.readInt();
            auto inv = InventoryItem(parser.readUint256(), type);
            if (type == InventoryItem::TransactionType || type == InventoryItem::BlockType) {
                auto findIter = m_downloadTargetIds.find(inv.hash());
                if (findIter == m_downloadTargetIds.end()) {
                    // new download target.
                    DownloadTarget dlt(inv);
                    dlt.sourcePeers.push_back(sourcePeerId);
                    m_downloadQueue.insert(std::make_pair(m_nextDownloadTarget, dlt));
                    m_downloadTargetIds.insert(std::make_pair(inv.hash(), m_nextDownloadTarget++));
                } else {
                    // add source to existing one
                    auto targetIter = m_downloadQueue.find(findIter->second);
                    assert(targetIter != m_downloadQueue.end());
                    targetIter->second.sourcePeers.push_back(sourcePeerId);
                }
            }
        }
    } catch (const std::exception &e) {
        logInfo() << "Inv messsage parsing failed" << e << "peer:" << sourcePeerId;
        m_connectionManager.punish(sourcePeerId);
    }
    logDebug() << " Queue size now" << m_downloadQueue.size();

    // call runQueue in a next event.
    m_strand.post(std::bind(&DownloadManager::runQueue, this));
}

void DownloadManager::parseTransaction(Tx tx, int sourcePeerId)
{
    auto hash = tx.createHash();
    std::unique_lock<std::mutex> lock(m_downloadsLock);
    const size_t downloadSlots = m_downloads.size();
    bool found = false;
    for (size_t i = 0; i < downloadSlots; ++i) {
        if (m_downloads[i].primary == sourcePeerId
                || m_downloads[i].secondary == sourcePeerId) {
            auto dlIter = m_downloadQueue.find(m_downloads[i].targetId);
            assert(dlIter != m_downloadQueue.end());
            if (dlIter->second.inv.type() == InventoryItem::TransactionType
                    && dlIter->second.inv.hash() == hash) {
                // mark download complete.
                auto tIter = m_downloadTargetIds.find(dlIter->second.inv.hash());
                if (m_downloadTargetIds.end() != tIter)
                    m_downloadTargetIds.erase(tIter);
                m_downloads[i] = ActiveDownload();
                m_downloadQueue.erase(dlIter);

                found = true;
                break;
            }
        }
    }
    if (!found) {
        logWarning() << "Peer" << sourcePeerId << "sent unsolicited tx. This breaks protocol";
        m_connectionManager.punish(sourcePeerId, 34);
    }
    try {
        for (auto iface : m_dataListeners)  {
            iface->newTransaction(tx);
        }
    }  catch (const std::exception &e) {
        // assume that anything wrong happening in the interface is our fault for not checking the
        // validity of the transaction.
        // Then we just blame the source peer for providing us with bad data.
        m_connectionManager.punish(sourcePeerId, 501);
    }
}

void DownloadManager::peerDisconnected(int connectionId)
{
    if (connectionId == m_peerDownloadingHeaders)
        m_peerDownloadingHeaders = -1;
}

void DownloadManager::reportDataFailure(int connectionId)
{
    m_connectionManager.punish(connectionId, 1001);
}

void DownloadManager::done(Action *action)
{
    std::unique_lock<std::mutex> lock(m_lock);
    for (auto iter = m_runningActions.begin(); iter != m_runningActions.end(); ++iter) {
        if (*iter == action) {
            m_runningActions.erase(iter);
            delete action;
            return;
        }
    }
}

void DownloadManager::addDataListener(DataListenerInterface *listener)
{
    std::unique_lock<std::mutex> lock(m_lock);
    for (auto iface : m_dataListeners) {
        if (iface == listener)
            return;
    }
    m_dataListeners.push_back(listener);
}

void DownloadManager::removeDataListener(DataListenerInterface *listener)
{
    std::unique_lock<std::mutex> lock(m_lock);
    for (auto iter = m_dataListeners.begin(); iter != m_dataListeners.end(); ++iter) {
        if (*iter == listener) {
            m_dataListeners.erase(iter);
            return;
        }
    }
}

void DownloadManager::addP2PNetListener(P2PNetInterface *listener)
{
    std::unique_lock<std::mutex> lock(m_lock);
    for (auto iface : m_listeners) {
        if (iface == listener)
            return;
    }
    m_listeners.push_back(listener);
}

void DownloadManager::removeP2PNetListener(P2PNetInterface *listener)
{
    std::unique_lock<std::mutex> lock(m_lock);
    for (auto iter = m_listeners.begin(); iter != m_listeners.end(); ++iter) {
        if (*iter == listener) {
            m_listeners.erase(iter);
            return;
        }
    }
}

const std::deque<P2PNetInterface *> &DownloadManager::p2pNetListeners()
{
    return m_listeners;
}

void DownloadManager::shutdown()
{
    std::unique_lock<std::mutex> lock(m_lock);
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;

    for (auto a : m_runningActions) {
        a->cancel();
    }
    m_connectionManager.shutdown();

    m_strand.post(std::bind(&DownloadManager::finishShutdown, this));
    m_waitVariable.wait(lock);
}

void DownloadManager::finishShutdown()
{
    assert(m_shuttingDown);
    std::unique_lock<std::mutex> lock(m_lock);
    m_waitVariable.notify_all();
}

void DownloadManager::runQueue()
{
    if (m_shuttingDown)
        return;
    std::unique_lock<std::mutex> lock(m_downloadsLock);
    auto iter = m_downloadQueue.begin();
    const size_t downloadSlots = m_downloads.size();
    for (size_t i = 0; i < downloadSlots; ++i) {
        if (m_downloads[i].targetId == 0) { // slot unoccupied.
            while (true) {
                if (iter == m_downloadQueue.end())
                    return; // nothing left to download

                const DownloadTarget &dt = iter->second;
                bool alreadyRunning = false;
                for (size_t x = 0; !alreadyRunning && x < downloadSlots; ++x) {
                    // first check if nobody is downloading this one yet.
                    alreadyRunning = m_downloads[x].targetId == iter->first;
                }
                if (alreadyRunning) {
                    ++iter;
                    continue;
                }
                if (dt.inv.type() == InventoryItem::BlockType) {
                    if (m_blockchain.isKnown(dt.inv.hash())) {
                        // hash already known. No need to download it.
                        auto tIter = m_downloadTargetIds.find(dt.inv.hash());
                        if (m_downloadTargetIds.end() != tIter)
                            m_downloadTargetIds.erase(tIter);
                        iter = m_downloadQueue.erase(iter);
                        continue;
                    }
                }

                assert(!dt.sourcePeers.empty());
                int preferredDownload = -1;
                for (auto peerId : dt.sourcePeers) {
                    // find a peer we assign the download to.
                    if (preferredDownload == -1) {
                        preferredDownload = peerId;
                    } else {
                        bool inUse = false;
                        for (size_t x = 0; !inUse && x < downloadSlots; ++x) {
                            if (m_downloads[x].primary == peerId || m_downloads[x].secondary == peerId) {
                                inUse = true;
                                break;
                            }
                        }
                        if (!inUse) {
                            break;
                            preferredDownload = peerId;
                        }
                    }
                }
                auto peer = m_connectionManager.peer(preferredDownload);
                assert(peer);
                if (peer) {
                    logInfo() << "Requesting DL for inv from peer:" << preferredDownload;
                    m_downloads[i].targetId = iter->first;
                    m_downloads[i].downloadStartTime = time(nullptr);
                    m_downloads[i].primary = preferredDownload;

                    if (dt.inv.type() == InventoryItem::TransactionType) {
                        Streaming::P2PBuilder builder(m_connectionManager.pool(40));
                        builder.writeCompactSize(1);
                        builder.writeInt(dt.inv.type());
                        builder.writeByteArray(dt.inv.hash(), Streaming::RawBytes);
                        peer->sendMessage(builder.message(Api::P2P::GetData));
                    }
                    else {
                        assert(dt.inv.type() == InventoryItem::BlockType);
                        m_connectionManager.requestHeaders(peer);
                    }
                }
                break;
            }
        }
    }
}

void DownloadManager::start()
{
    // addAction<SyncChainAction>();
    // addAction<FillAddressDBAction>();
}
