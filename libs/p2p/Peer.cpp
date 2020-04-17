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
#include "Peer.h"
#include "ConnectionManager.h"
#include "PrivacySegment.h"
#include "Blockchain.h"
#include "BlockHeader.h"

#include <streaming/P2PParser.h>
#include <streaming/P2PBuilder.h>

#include <hash.h>

#include <streaming/MessageBuilder.h>
#include <boost/asio/error.hpp>
#include <primitives/FastTransaction.h>

Peer::Peer(ConnectionManager *parent, NetworkConnection && server, const PeerAddress &address)
    :  m_peerAddress(address),
      m_con(std::move(server)),
      m_connectionManager(parent)
{
    assert(m_peerAddress.isValid());
    m_peerAddress.setInUse(true);
    m_timeOffset = time(nullptr);

    m_con.setOnConnected(std::bind(&Peer::connected, this, std::placeholders::_1));
    m_con.setOnDisconnected(std::bind(&Peer::disconnected, this, std::placeholders::_1));
    m_con.setOnIncomingMessage(std::bind(&Peer::processMessage, this, std::placeholders::_1));
    m_con.setMessageHeaderLegacy(true);
    m_con.connect();
}

Peer::~Peer()
{
    assert(m_peerAddress.isValid());
    m_peerAddress.setInUse(false);
}

void Peer::disconnect()
{
    logDebug() << "I asked to disconnect. Peer:" << connectionId();
    m_con.shutdown();
}

void Peer::connected(const EndPoint &endPoint)
{
    m_peerStatus = Connected;
    logDebug() << "connected. Peer:" << connectionId();

    // send the version message.
    auto &pool = m_connectionManager->pool(400);
    Streaming::P2PBuilder builder(pool);
    builder.writeInt(PROTOCOL_VERSION);
    builder.writeLong(m_connectionManager->servicesBitfield());
    builder.writeLong((uint64_t) time(nullptr));
    // Version msg: target address
    builder.writeLong((uint64_t) 2); // services again
    char buf[16];
    memset(buf, 0, 16);
    buf[10] = buf[11] = 0xff; // mark address as an IPv4 one
    builder.writeByteArray(buf, 16, Streaming::RawBytes);
    builder.writeWord(endPoint.announcePort);
    // Version msg: my address
    builder.writeLong(3); // services again
    builder.writeByteArray(buf, 16, Streaming::RawBytes);
    builder.writeWord(7); // port
    // Version msg: my status
    builder.writeLong(m_connectionManager->appNonce());
    builder.writeString(m_connectionManager->userAgent(), Streaming::WithLength);
    builder.writeInt(m_connectionManager->blockHeight());
    builder.writeBool(/* relay-txs */ false);

    // version is always the first thing they expect on connect
    Message message = builder.message(Api::P2P::Version);
    logDebug().nospace() << "peer: " << connectionId() << ", sending message (" << message.body().size() << "bytes)";
    m_con.send(message);
    m_con.send(Message(Api::LegacyP2P, Api::P2P::PreferHeaders));
}

void Peer::disconnected(const EndPoint &)
{
    logDebug() << "Disconnected. Peer:" << connectionId();
    if (m_peerStatus == Connected)
        m_peerStatus = Disconnected;
    m_connectionManager->disconnected(this);
}

void Peer::processMessage(const Message &message)
{
    try {
        logDebug() << "Peer:" << connectionId() << "messageId:"
                             << message.header().constData() << "of" << message.body().size() << "bytes";
        if (message.messageId() == Api::P2P::Version) {
            Streaming::P2PParser parser(message);
            m_protocolVersion = parser.readInt();
            m_services = parser.readLong();
            m_timeOffset = time(nullptr) - parser.readLong();

            // address
            parser.skip(8 + 16 + 2); // IP (and services and port) of them
            parser.skip(8 + 16 + 2); // IP of me.
            parser.skip(8); // nonce
            m_userAgent = parser.readString();
            m_startHeight = parser.readInt();
            m_relaysTransactions = parser.readBool();

            logInfo() << "Peer:" << connectionId() << "is connected to" << m_userAgent << m_peerAddress.peerAddress();
            m_con.send(Message(Api::LegacyP2P, Api::P2P::VersionAck));
            m_connectionManager->connectionEstablished(this);
            m_peerAddress.successfullyConnected();
        }
        else if (message.messageId() == Api::P2P::Ping) {
            m_con.send(Message(message.body(), Api::LegacyP2P, Api::P2P::Pong));
        }
        else if (message.messageId() == Api::P2P::PreferHeaders) {
            m_preferHeaders = true;
        }
        else if (message.messageId() == Api::P2P::Headers) {
            m_receivedHeaders = true;
            m_connectionManager->addBlockHeaders(message, connectionId());
        }
        else if (message.messageId() == Api::P2P::RejectData) {
            Streaming::P2PParser parser(message);
            logFatal() << "Reject received for" << parser.readString() // TODO
                       << parser.readByte() << parser.readString();
        }
        else if (message.messageId() == Api::P2P::Addresses) {
            m_connectionManager->addAddresses(message, connectionId());
        }
        else if (message.messageId() == Api::P2P::Inventory) {
            m_connectionManager->addInvMessage(message, connectionId());
        }
        else if (message.messageId() == Api::P2P::Data_Transaction) {
            Tx tx(message.body());
            if (m_segment)
                processTransaction(tx);
            else
                m_connectionManager->addTransaction(tx, connectionId());
        }
        else if (message.messageId() == Api::P2P::Data_MerkleBlock) {
            if (!m_segment) { // Received merkleblock without asking for one
                m_connectionManager->punish(PUNISHMENT_MAX);
                return;
            }
            Streaming::P2PParser parser(message);
            auto header = BlockHeader::fromMessage(parser);
            int blockHeight = m_connectionManager->blockHeightFor(header.createHash());
            if (blockHeight == -1) { // not on our chain (anymore)
                m_connectionManager->punish(PUNISHMENT_MAX);
                return;
            }
            CPartialMerkleTree tree = CPartialMerkleTree::construct(parser);
            if (tree.ExtractMatches(m_transactionHashes) != header.hashMerkleRoot) {
                m_transactionHashes.clear();
                m_merkleBlockHeight = -1;
                throw Streaming::ParsingException("Bad merkle tree received");
            }
            m_merkleBlockHeight = blockHeight;
            m_lastReceivedMerkle = blockHeight;
            m_segment->blockSynched(blockHeight);
            logDebug() << "Merkle received by" << connectionId() << "height:" << blockHeight;

            if (m_lastReceivedMerkle == m_merkleDownloadTo - 1) {
                m_merkleDownloadFrom = m_merkleDownloadTo;
                // we limit our INVs to 100 per request.  Notice that the protocol allows for 50000
                m_merkleDownloadTo = std::min(m_merkleDownloadFrom + 100, m_connectionManager->blockHeight() + 1);
                requestMerkleBlocks();
            }
        }
    } catch (const Streaming::ParsingException &e) {
        logCritical() << "Parsing failure" << e << "peer=" << m_con.connectionId();
        m_peerAddress.punishPeer(200);
        m_peerStatus = IncompatiblePeer; // makes sure we get removed on disconnect.
        m_con.disconnect();
    }
}

void Peer::sendFilter()
{
    assert(m_segment);
    auto buf = m_segment->writeFilter(m_connectionManager->pool(0));
    m_con.send(Message(buf, Api::LegacyP2P, Api::P2P::FilterLoad));
    m_bloomUploadHeight = m_segment->lastBlockSynched();
}

void Peer::sendFilter(const CBloomFilter &bloom, int blockHeight)
{
    Streaming::P2PBuilder builder(m_connectionManager->pool(bloom.GetSerializeSize(0, 0)));
    bloom.store(builder);
    m_con.send(Message(builder.buffer(), Api::LegacyP2P, Api::P2P::FilterLoad));
    m_bloomUploadHeight = blockHeight;
}

int Peer::lastReceivedMerkle() const
{
    return m_lastReceivedMerkle;
}

bool Peer::merkleDownloadInProgress() const
{
    return m_merkleDownloadFrom >= m_bloomUploadHeight // started
            && m_merkleDownloadFrom < m_merkleDownloadTo; // and has not stopped yet
}

void Peer::startMerkleDownload(int from)
{
    assert(m_segment);
    if (m_bloomUploadHeight < m_segment->filterChangedHeight() // filter changed since we uploaded it.
            && m_merkleDownloadFrom >= m_segment->filterChangedHeight()) // unless I'm the one that changed it
        sendFilter(); // then send updated filter

    m_merkleDownloadFrom = from;
    // we limit our INVs to 100 per message.  Notice that the protocol allows for 50000
    m_merkleDownloadTo = std::min(from + 100, m_connectionManager->blockHeight() + 1);
    requestMerkleBlocks();
}

void Peer::requestMerkleBlocks()
{
    const int count = m_merkleDownloadTo - m_merkleDownloadFrom;
    if (count == 0)
        return;
    Streaming::P2PBuilder builder(m_connectionManager->pool(40 * count));
    builder.writeCompactSize(count);
    for (int i = m_merkleDownloadFrom; i < m_merkleDownloadTo; ++i) {
        // write INV data-type
        builder.writeInt(3); // MSG_FILTERED_BLOCK aka MSG_MERKLEBLOCK
        builder.writeByteArray(m_connectionManager->blockHashFor(i), Streaming::RawBytes);
    }
    m_con.send(builder.message(Api::P2P::GetData));
}

int Peer::bloomUploadHeight() const
{
    return m_bloomUploadHeight;
}

bool Peer::receivedHeaders() const
{
    return m_receivedHeaders;
}

void Peer::setPrivacySegment(PrivacySegment *ps)
{
    assert(ps);
    if (ps != m_segment) {
        assert(m_segment == nullptr);
        m_segment = ps;
        // TODO subscribe to segment callbacks
    }
    sendFilter();
}

void Peer::processTransaction(const Tx &tx)
{
    if (m_merkleBlockHeight > 0) {
        assert(m_segment);
        const uint256 txHash = tx.createHash();
        for (auto iter = m_transactionHashes.begin(); iter != m_transactionHashes.end(); ++iter) {
            if (txHash == *iter) {
                m_transactionHashes.erase(iter);
                int bh = m_merkleBlockHeight;
                if (m_transactionHashes.empty()) {
                    m_merkleBlockHeight = -1;
                }
                assert(m_segment);
                m_segment->newTransaction(m_merkleHeader, bh, tx);
                return;
            }
        }
    }
    // must be a mempool transaction then.
    m_segment->newTransaction(tx);
}

bool Peer::preferHeaders() const
{
    return m_preferHeaders;
}

bool Peer::relaysTransactions() const
{
    return m_relaysTransactions;
}

int Peer::startHeight() const
{
    return m_startHeight;
}

std::string Peer::userAgent() const
{
    return m_userAgent;
}

int Peer::protocolVersion() const
{
    return m_protocolVersion;
}

int Peer::timeOffset() const
{
    return m_timeOffset;
}

uint64_t Peer::services() const
{
    return m_services;
}
