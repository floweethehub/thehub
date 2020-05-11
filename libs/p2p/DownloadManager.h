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
#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include "ConnectionManager.h"
#include "Action.h"
#include "Blockchain.h"
#include "InventoryItem.h"
#include "DataListenerInterface.h"
#include <uint256.h>

#include <condition_variable>
#include <deque>
#include <vector>
#include <boost/asio/io_context_strand.hpp>
#include <boost/unordered_map.hpp>

class P2PNetInterface;

/**
 * A download manager as its name implies should manage what is to be downloaded.
 * various peers could tell us about stuff that needs to be downloaded and we should
 * round-robin over those that supply this info to actually download it.
 */
class DownloadManager
{
public:
    /**
     * @brief DownloadManager constructor.
     * @param service the IO-service. See WorkerThreads
     * @param basedir the directory to load and save state data.
     */
    DownloadManager(boost::asio::io_service &service, const boost::filesystem::path &basedir);

    // start reaching out and synchronizing.
    void start();

    const ConnectionManager &connectionManager() const;
    ConnectionManager &connectionManager();

    inline Blockchain &blockchain() {
        return m_blockchain;
    }
    inline const Blockchain &blockchain() const {
        return m_blockchain;
    }

    inline uint64_t servicesBitfield() const {
        return m_connectionManager.servicesBitfield();
    }
    inline void setServicesBitfield(const uint64_t &servicesBitfield) {
        m_connectionManager.setServicesBitfield(servicesBitfield);
    }

    inline int blockHeight() const {
        return m_connectionManager.blockHeight();
    }
    void headersDownloadFinished(int newBlockHeight, int peerId);

    inline boost::asio::io_context::strand &strand() {
        return m_strand;
    }
    inline boost::asio::io_service &service() {
        return m_connectionManager.service();
    }

    /**
     * returns peerId that is downloading headers. Or -1 if nobody is.
     */
    inline int peerDownloadingHeaders() const {
        return m_peerDownloadingHeaders;
    }

    void parseInvMessage(Message message, int sourcePeerId);
    void parseTransaction(Tx tx, int sourcePeerId);

    void peerDisconnected(int connectionId);

    // Callback to let us know the data is invalid.
    // This typically leads us to ban the peer.
    void reportDataFailure(int connectionId);

    template<class T>
    T* addAction() {
        T *t;
        { // lock scope
            std::unique_lock<std::mutex> lock(m_lock);
            if (m_shuttingDown) return nullptr;
            for (auto a : m_runningActions) {
                t = dynamic_cast<T*>(a);
                if (t)
                    return t;
            }
            t =new T(this);
            m_runningActions.push_back(t);
        }
        t->start();
        return t;
    }

    void done(Action *action);

    void addDataListener(DataListenerInterface *listener);
    void removeDataListener(DataListenerInterface *listener);

    void addP2PNetListener(P2PNetInterface *listener);
    void removeP2PNetListener(P2PNetInterface *listener);

    const std::deque<P2PNetInterface*> &p2pNetListeners();

    void shutdown();

private:
    void finishShutdown();

    boost::asio::io_context::strand m_strand;
    ConnectionManager m_connectionManager;
    Blockchain m_blockchain;
    std::deque<P2PNetInterface*> m_listeners;

    std::deque<Action*> m_runningActions;
    mutable std::mutex m_lock;
    std::atomic<bool> m_shuttingDown;
    std::condition_variable m_waitVariable;

    int m_peerDownloadingHeaders = -1;

    std::vector<DataListenerInterface*> m_dataListeners;

    void runQueue(); ///< find new items to download

    struct DownloadTarget {
        DownloadTarget(const InventoryItem &item) : inv(item) {}
        InventoryItem inv;
        std::vector<int> sourcePeers;
    };

    struct ActiveDownload {
        uint32_t targetId = 0;
        uint32_t downloadStartTime = 0;
        int primary = -1, secondary = -1; // downloaders.
    };

    mutable std::mutex m_downloadsLock;
    std::map<uint32_t, DownloadTarget> m_downloadQueue;
    typedef boost::unordered_map<uint256, uint32_t, HashShortener> DownloadTargetIds;
    DownloadTargetIds m_downloadTargetIds; // uint256 -> downloadQueue-Id
    uint32_t m_nextDownloadTarget = 0;
    std::array<ActiveDownload, 10> m_downloads;
};

#endif
