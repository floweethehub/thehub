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
#ifndef CONNECTIONMANAGER_h
#define CONNECTIONMANAGER_h

#include "PeerAddressDB.h"

#include <networkmanager/NetworkManager.h>
#include <NetworkEndPoint.h>
#include <primitives/FastTransaction.h>

#include <boost/asio/deadline_timer.hpp>

#include <set>
#include <mutex>
#include <map>
#include <deque>
#include <cstdint>

class DownloadManager;
class PrivacySegment;
class Peer;
class uint256;

namespace Streaming {
    class BufferPool;
}

class ConnectionManager
{
public:
    ConnectionManager(boost::asio::io_service &service, DownloadManager *parent);

    // return a pool for the current thread;
    Streaming::BufferPool &pool(int reserveSize);

    void connect(PeerAddress &address);
    void disconnect(Peer *peer);

    uint64_t servicesBitfield() const;
    void setServicesBitfield(const uint64_t &servicesBitfield);

    int blockHeight() const;
    void setBlockHeight(int blockHeight);
    int blockHeightFor(const uint256 &blockId);
    uint256 blockHashFor(int height);

    uint64_t appNonce() const;

    void connectionEstablished(Peer *peer);

    void addBlockHeaders(const Message &message, int sourcePeerId);
    void addAddresses(const Message &message, int sourcePeerId);
    void addInvMessage(const Message &message, int sourcePeerId);
    void addTransaction(const Tx &message, int sourcePeerId);

    inline boost::asio::io_service &service() {
        return m_ioService;
    }

    void punish(std::shared_ptr<Peer> peer, int amount = 250);
    void punish(int connectionId, int amount = 250);
    void requestHeaders(std::shared_ptr<Peer> peer);

    std::deque<std::shared_ptr<Peer> > connectedPeers() const;

    inline const PeerAddressDB &peerAddressDb() const {
        return m_peerAddressDb;
    }
    inline PeerAddressDB &peerAddressDb() {
        return m_peerAddressDb;
    }

    std::shared_ptr<Peer> peer(int connectionId) const;

    void addPrivacySegment(PrivacySegment *ps);
    void removePrivacySegment(PrivacySegment *ps);

    void setUserAgent(const std::string &userAgent);

    inline const std::string &userAgent() const {
        return m_userAgent;
    }

    int peerCount() const;

    std::deque<PrivacySegment *> segments() const;

    void shutdown();

private:
    void cron(const boost::system::error_code &error);
    void handleError(int remoteId, const boost::system::error_code &error);
    void handleError_impl(int remoteId, const boost::system::error_code &error);

    // m_lock should already be taken by caller
    void removePeer(Peer *peer);

    uint64_t m_appNonce;
    uint64_t m_servicesBitfield = 0;
    int m_blockHeight = 0;

    mutable std::mutex m_lock;
    std::atomic<bool> m_shuttingDown;
    std::map<int, std::shared_ptr<Peer>> m_peers;
    std::set<int> m_connectedPeers;

    boost::asio::io_service &m_ioService;
    boost::asio::deadline_timer m_cronTimer;
    NetworkManager m_network;
    PeerAddressDB m_peerAddressDb;
    DownloadManager *m_dlManager; // parent
    std::string m_userAgent;

    std::deque<PrivacySegment*> m_segments;
};

#endif
