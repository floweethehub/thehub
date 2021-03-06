/*
 * This file is part of the Flowee project
 * Copyright (C) 2020 Tom Zander <tom@flowee.org>
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
#include "CleanPeersAction.h"

#include <primitives/FastTransaction.h>
#include <streaming/P2PParser.h>
#include <streaming/P2PBuilder.h>

DownloadManager::DownloadManager(boost::asio::io_service &service, const boost::filesystem::path &basedir, P2PNet::Chain chain)
    : m_strand(service),
      m_chain(chain),
      m_connectionManager(service, basedir, this),
      m_blockchain(this, basedir, chain),
      m_shuttingDown(false)
{
    m_connectionManager.setBlockHeight(m_blockchain.height());
    m_isBehind = !isChainUpToDate();

    // create basedir, and fail-fast if we don't have writing rights to do that.
    try {
        boost::filesystem::create_directories(basedir);
    } catch (const boost::filesystem::filesystem_error&) {
        if (!boost::filesystem::exists(basedir) || !boost::filesystem::is_directory(basedir)) {
            logFatal() << "Failed to create datadir" << basedir.string();
            throw;
        }
        // errors like "already exists" are safe to ignore.
    }
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
    // The blockchain lets us know the result of a successful headers-download.
    assert(m_strand.running_in_this_thread());
    if (m_peerDownloadingHeaders == peerId)
        m_peerDownloadingHeaders = -1;

    auto peer = m_connectionManager.peer(peerId);
    if (peer.get()) {
        peer->peerAddress().gotGoodHeaders();
        peer->updatePeerHeight(newBlockHeight);
    }

    m_connectionManager.setBlockHeight(newBlockHeight);
    getMoreHeaders();
    for (auto iface : m_listeners) {
        iface->blockchainHeightChanged(newBlockHeight);
    }
    m_notifications.notifyNewBlock(newBlockHeight);
    if (m_isBehind && isChainUpToDate()) {
        m_isBehind = false;
        for (auto iface : m_dataListeners)  {
            iface->headerSyncComplete();
        }
    }

    addAction<SyncSPVAction>();
}

void DownloadManager::getMoreHeaders()
{
    if (m_peerDownloadingHeaders == -1) { // check if we need to download more of them.
        // TODO use the fastest peer.
        for (auto &p : m_connectionManager.connectedPeers()) {
            if (p->startHeight() > blockHeight()) {
                m_peerDownloadingHeaders = p->connectionId();
                m_connectionManager.requestHeaders(p);
                return;
            }
        }
    }
}

void DownloadManager::parseInvMessage(Message message, int sourcePeerId)
{
    if (m_shuttingDown)
        return;
    // this is called as a result of an INV received by a peer.
    // We check this and insert into the m_downloadQueue a target to download.
    try {
        Streaming::P2PParser parser(message);
        const size_t count = parser.readCompactInt();
        logDebug() << "Received" << count << "Inv messages";
        std::unique_lock<std::mutex> lock(m_downloadsLock);
        for (size_t i = 0; i < count; ++i) {
            uint32_t type = parser.readInt();
            auto inv = InventoryItem(parser.readUint256(), type);

            // if block type, check if we already know about it
            if (type == InventoryItem::BlockType) {
                auto height = m_blockchain.blockHeightFor(inv.hash());
                if (height > 0) {
                    // a block-inv we already have seen and approved of.
                    auto peer = m_connectionManager.peer(sourcePeerId);
                    if (peer)
                        peer->updatePeerHeight(height);
                    // no need for further action.
                    continue;
                }
            }

            // otherwise we update the downloads queue
            if (type == InventoryItem::TransactionType || type == InventoryItem::BlockType
                    || type == InventoryItem::DoubleSpendType) {
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
    // we get called by the peer about a transaction just received.
    // Now we find the downloads data that requested it and update
    // m_downloads and m_downloadTargetIds and m_downloadQueue.
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
    try {
        m_blockchain.save();
    } catch (const std::exception &e) {
        logFatal() << "P2PNet: blockchain-saving during shutdown failed" << e;
    }

    m_strand.post(std::bind(&DownloadManager::finishShutdown, this));
    m_waitVariable.wait(lock);
}

void DownloadManager::finishShutdown()
{
    assert(m_shuttingDown);
    std::unique_lock<std::mutex> lock(m_lock);
    m_waitVariable.notify_all();
}

P2PNet::Chain DownloadManager::chain() const
{
    return m_chain;
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

            // iterate through downloadqueue to find a new job
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
                    auto height = m_blockchain.blockHeightFor(dt.inv.hash());
                    if (height > 0) {
                        // hash already known.
                        // No need to download it.
                        auto tIter = m_downloadTargetIds.find(dt.inv.hash());
                        if (m_downloadTargetIds.end() != tIter)
                            m_downloadTargetIds.erase(tIter);

                        // let the peer know the height that is related to the INV
                        for (auto id : iter->second.sourcePeers) {
                            auto peer = m_connectionManager.peer(id);
                            if (peer)
                                peer->updatePeerHeight(height);
                        }
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

                    if (dt.inv.type() == InventoryItem::TransactionType || dt.inv.type() == InventoryItem::DoubleSpendType) {
                        Streaming::P2PBuilder builder(m_connectionManager.pool(40));
                        builder.writeCompactSize(1);
                        builder.writeInt(dt.inv.type());
                        builder.writeByteArray(dt.inv.hash(), Streaming::RawBytes);
                        peer->sendMessage(builder.message(Api::P2P::GetData));
                    }
                    else if (dt.inv.type() == InventoryItem::BlockType) {
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
    addAction<SyncChainAction>();
    addAction<FillAddressDBAction>();
    addAction<SyncSPVAction>();
    addAction<CleanPeersAction>();
}
