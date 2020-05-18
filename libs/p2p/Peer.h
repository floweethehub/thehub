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
#ifndef PEER_H
#define PEER_H

#include "PeerAddressDB.h"
#include "BlockHeader.h"
#include "PrivacySegmentListener.h"

#include <networkmanager/NetworkConnection.h>
#include <uint256.h>
#include <deque>

class PrivacySegment;
class Blockchain;
class Tx;
class ConnectionManager;
class CBloomFilter;

class Peer : public std::enable_shared_from_this<Peer>, private PrivacySegmentListener
{
public:
    enum PeerStatus {
        Connecting,
        Connected,
        ShuttingDown
    };

    explicit Peer(ConnectionManager *parent, const PeerAddress &address);
    ~Peer();

    void connect(NetworkConnection && server);

    /**
     * @brief shutdown will cause this peer stop processing network request.
     *
     * Calling this is required for the shared_ptr based peer to be deletable.
     * Specificially: it breaks a cyclic loop with the network layer.
     */
    void shutdown();

    /// Returns the services bitfield of the remote peer.
    uint64_t services() const;

    /// Returns the amount of seconds that this peer is ahead/behind us.
    int timeOffset() const;

    /// Return the protocol version the remote peer reported.
    int protocolVersion() const;

    /// Returns the internal ID our network connection is on.
    inline int connectionId() const {
        return m_con.connectionId();
    }

    /// Returns the user-agent of the remote peer.
    std::string userAgent() const;

    /// Returns the blockheight the peer reported at connection time.
    int startHeight() const;

    /// Returns if the remote peer is willing to relay transactions.
    bool relaysTransactions() const;

    /// Returns if the remote peer prefers headers over INV messages for new block announcements.
    bool preferHeaders() const;

    /// Return the current connection status of this peer.
    PeerStatus status() const {
        return m_peerStatus;
    }

    /// Returns true if the peers services indicate it supplies serving blockdata over the network.
    bool supplies_network() {
        return (m_services & 1) == 1;
    }
    // bip 159
    /// Returns true if the peer services indicate it is partial (pruned) blockdata it serves over the net.
    bool supplies_partialNetwork() {
        return (m_services & 2) != 0;
    }

    /// Returns true if the peer supplies bloom services.
    bool supplies_bloom() {
        return (m_services & 4) != 0;
    }

    /// Sends a message to the remote peer.
    inline void sendMessage(const Message &message) {
        m_con.send(message);
    }

    /// Returns the address of the remote peer.
    inline PeerAddress& peerAddress() { return m_peerAddress; }
    /// Returns the address of the remote peer.
    inline const PeerAddress& peerAddress() const { return m_peerAddress; }

    /// peer has received the response to 'getheaders', implying it is following the same chain as us.
    /// @see PeerAddress::gotGoodHeaders() for a historical one.
    /// @see requestedHeaders()
    bool receivedHeaders() const;

    /// Peer asked for getheaders, see @receivedHeaders()
    bool requestedHeader() const;

    /// set if the peer requested headers.
    void setRequestedHeader(bool requestedHeader);

    /// Assigns this peer a wallet in the shape of a PrivacySegment.
    void setPrivacySegment(PrivacySegment *ps);
    /// Return the set privacy segment, if any.
    inline PrivacySegment *privacySegment() const {
        return m_segment;
    }

    /// the blockHeight we were at when we send the bloom filter to the peer
    int bloomUploadHeight() const;

    /// the blockheight of the last merkle block we received.
    int lastReceivedMerkle() const;

    /// Return true if the merkle-block based fetches are in-progress.
    bool merkleDownloadInProgress() const;

    /// start downloads of merkle (aka SPV) blocks to the current height
    void startMerkleDownload(int from);
    /// send to the peer the bloom filter arg, with the promise that that it looked like that at \a blockHeight
    void sendFilter(const CBloomFilter &bloom, int blockHeight);

    /// Return the timestamp of first-connection time.
    uint32_t connectTime() const;

private:
    void connected(const EndPoint&);
    void disconnected(const EndPoint&);
    void processMessage(const Message &message);
    void processTransaction(const Tx &tx);
    void requestMerkleBlocks();

    /// sends the bloom filter to peer.
    void sendFilter_priv();

    // PrivacySegmentListener interface
    void filterUpdated();

    uint64_t m_services = 0;
    int m_timeOffset = 0;
    uint32_t m_connectTime = 0;
    int m_protocolVersion = 0;
    std::string m_userAgent;
    int m_startHeight = 0;
    bool m_relaysTransactions = false;
    bool m_preferHeaders = false;
    bool m_requestedHeader = false;
    bool m_receivedHeaders = false;

    PeerAddress m_peerAddress;
    std::atomic<PeerStatus> m_peerStatus;

    NetworkConnection m_con;
    ConnectionManager * const m_connectionManager;

    // privacy segment data
    PrivacySegment *m_segment = nullptr;
    int m_bloomUploadHeight = 0;
    int m_lastReceivedMerkle = 0;
    int m_merkleDownloadFrom = -1;
    int m_merkleDownloadTo = -1;

    // SPV merkle block data
    int m_merkleBlockHeight = -1;
    std::vector<uint256> m_transactionHashes;
    std::deque<Tx> m_blockTransactions;
    BlockHeader m_merkleHeader;
};

#endif
