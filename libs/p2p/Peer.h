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

#include <networkmanager/NetworkConnection.h>
#include <streaming/BufferPool.h>
#include <uint256.h>

#include <deque>

class PrivacySegment;
class Blockchain;
class Tx;
class ConnectionManager;
class CBloomFilter;

class Peer
{
public:
    enum PeerStatus {
        Connecting,
        Connected,
        ShuttingDown
    };

    explicit Peer(ConnectionManager *parent, NetworkConnection && server, const PeerAddress &address);

    ~Peer();

    /**
     * @brief shutdown will cause this peer to be shut down and deleted.
     * You should not use this peer object after calling this method anymore.
     */
    void shutdown();

    uint64_t services() const;

    int timeOffset() const;

    int protocolVersion() const;

    inline int connectionId() const {
        return m_con.connectionId();
    }

    std::string userAgent() const;

    int startHeight() const;

    bool relaysTransactions() const;

    bool preferHeaders() const;

    PeerStatus status() const {
        return m_peerStatus;
    }

    bool supplies_network() {
        return (m_services & 1) == 1;
    }
    // bip 159
    bool supplies_partialNetwork() {
        return (m_services & 2) != 0;
    }

    bool supplies_bloom() {
        return (m_services & 4) != 0;
    }

    inline void sendMessage(const Message &message) {
        m_con.send(message);
    }

    inline PeerAddress& peerAddress() { return m_peerAddress; }
    inline const PeerAddress& peerAddress() const { return m_peerAddress; }

    // peer has received the response to 'getheaders', implying it is following the same chain as us.
    bool receivedHeaders() const;

    void setPrivacySegment(PrivacySegment *ps);
    inline PrivacySegment *privacySegment() const {
        return m_segment;
    }

    // the blockHeight we were at when we send the bloom filter to the peer
    int bloomUploadHeight() const;

    // the blockheight of the last merkle block we received.
    int lastReceivedMerkle() const;

    bool merkleDownloadInProgress() const;

    // start downloads of merkle (aka SPV) blocks to the current height
    void startMerkleDownload(int from);
    /// send to the peer the bloom filter arg, with the promise that that it looked like that at \a blockHeight
    void sendFilter(const CBloomFilter &bloom, int blockHeight);

    uint32_t connectTime() const;

private:
    void connected(const EndPoint&);
    void disconnected(const EndPoint&);
    void processMessage(const Message &message);
    void processTransaction(const Tx &tx);
    void requestMerkleBlocks();
    void finalShutdown();

    void sendFilter();

    uint64_t m_services = 0;
    int m_timeOffset = 0;
    uint32_t m_connectTime = 0;
    int m_protocolVersion = 0;
    std::string m_userAgent;
    int m_startHeight = 0;
    bool m_relaysTransactions = false;
    bool m_preferHeaders = false;
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
    BlockHeader m_merkleHeader;
};

#endif
