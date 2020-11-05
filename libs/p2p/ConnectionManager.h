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
class BroadcastTxData;

namespace Streaming {
    class BufferPool;
}

/**
 * @brief The ConnectionManager class owns all the Peer objects and handles their lifespan.
 */
class ConnectionManager
{
public:
    ConnectionManager(boost::asio::io_service &service, const boost::filesystem::path &basedir, DownloadManager *parent);

    // return a pool for the current thread;
    Streaming::BufferPool &pool(int reserveSize);

    /// create a new Peer for address, if it isn't already connected.
    void connect(PeerAddress &address);
    /// disconnect this peer.
    void disconnect(const std::shared_ptr<Peer> &peer);

    /// the network services we support.
    uint64_t servicesBitfield() const;
    /// set the network services we support.
    void setServicesBitfield(const uint64_t &servicesBitfield);

    /// Sync-height, cached.
    int blockHeight() const;
    /// You probably should not call this. Its for the Blockchain
    void setBlockHeight(int blockHeight);
    /// Return the blockheight for the argument block-hash.
    int blockHeightFor(const uint256 &blockId);
    /// return the blockhash for a certain block-height.
    uint256 blockHashFor(int height);

    /// a randomly generated nonce, to avoid connecting to self.
    uint64_t appNonce() const;

    /// slot that peers call to notify us they connected and finished handshake.
    void connectionEstablished(const std::shared_ptr<Peer> &peer);

    /// A peer sends us blockheaders it received.
    void addBlockHeaders(const Message &message, int sourcePeerId);
    /// A peer sends up addresses it receved.
    void addAddresses(const Message &message, int sourcePeerId);
    // A peer sends us INV messages it received.
    void addInvMessage(const Message &message, int sourcePeerId);
    /// A peer sends us a Transaction it received
    void addTransaction(const Tx &message, int sourcePeerId);

    inline boost::asio::io_service &service() {
        return m_ioService;
    }

    /// Punish a peer after detecting misbehavior.
    bool punish(const std::shared_ptr<Peer> &peer, int amount = 250);
    /// conveninience overload
    bool punish(int connectionId, int amount = 250);
    /// Send a request to peer for headers, to identify their chain.
    void requestHeaders(const std::shared_ptr<Peer> &peer);

    /// returns a list of connected peers.
    std::deque<std::shared_ptr<Peer> > connectedPeers() const;

    /// Share the peer addresses DB
    inline const PeerAddressDB &peerAddressDb() const {
        return m_peerAddressDb;
    }
    /// Share the peer addresses DB
    inline PeerAddressDB &peerAddressDb() {
        return m_peerAddressDb;
    }

    /// Return a peer by connection-id.
    std::shared_ptr<Peer> peer(int connectionId) const;

    /// register a privacy segment to be assigned to peers.
    void addPrivacySegment(PrivacySegment *ps);
    /// remove a privacy segment from our list. No existing peers will be affected.
    void removePrivacySegment(PrivacySegment *ps);

    /// Note the network-identifying string we will announce ourself as.
    void setUserAgent(const std::string &userAgent);

    /// returns our network-identifying string we will announce ourself as.
    inline const std::string &userAgent() const {
        return m_userAgent;
    }

    /**
     * Allow apps to broadcast a transaction to peers.
     *
     * This method takes a BroadcastTxData which has several reasons:
     * - It combines the actual transaction and the private segment.
     * - It gives the ConnectionManager some call-backs to report
     *   success or failure.
     * - It allows the caller of this method to set the lifetime
     *   of the broadcast order by simply deleting the txOwner when
     *   it wants to stop the broadcast.
     *
     * This class will keep a weak-pointer to the txOwner only, so lifetime
     * management lies with the caller.
     */
    void broadcastTransaction(const std::shared_ptr<BroadcastTxData> &txOwner);

    /// returns the amount of peers we currently have. Even unconnected ones.
    int peerCount() const;

    /// returns a copy of the segments list we hold.
    std::deque<PrivacySegment *> segments() const;

    /// Shut down this connection manager, and the peers as well as connections.
    /// It is required to call this prior to calling the destructor in order to
    /// cleanly shut down the system and stop all tasks in all threads.
    void shutdown();

    /**
     * @brief setMessageQueueSize allows a configuration of how many buffers a connection should have.
     * @param size the amount of messages we queue. The variable should be positive and fit in a `short` integer.
     *
     * @see NetworkConnection::setMessageQueueSizes
     *
     * Notice that this only affects newly created connections.
     */
    void setMessageQueueSize(int size);

private:
    void cron(const boost::system::error_code &error);
    void handleError(int remoteId, const boost::system::error_code &error);
    void handleError_impl(int remoteId, const boost::system::error_code &error);

    // m_lock should already be taken by caller
    void removePeer(const std::shared_ptr<Peer> &peer);

    uint64_t m_appNonce;
    uint64_t m_servicesBitfield = 0;
    int m_blockHeight = 0;

    short m_queueSize = 200; // config setting for the NetworkConnection buffer-size

    mutable std::mutex m_lock;
    std::atomic<bool> m_shuttingDown;
    std::map<int, std::shared_ptr<Peer>> m_peers;
    std::set<int> m_connectedPeers;

    boost::asio::io_service &m_ioService;
    boost::asio::deadline_timer m_cronTimer;
    PeerAddressDB m_peerAddressDb;
    NetworkManager m_network;
    DownloadManager *m_dlManager; // parent
    std::string m_userAgent;
    boost::filesystem::path m_basedir;

    std::deque<PrivacySegment*> m_segments;
    std::deque<std::weak_ptr<BroadcastTxData> > m_transactionsToBroadcast;
};

#endif
