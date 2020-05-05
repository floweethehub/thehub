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
#include "ConnectionManager.h"
#include "DownloadManager.h"
#include "P2PNetInterface.h"
#include "Peer.h"
#include "PrivacySegment.h"

#include <streaming/BufferPool.h>
#include <streaming/P2PBuilder.h>
#include <streaming/P2PParser.h>
#include <APIProtocol.h>
#include <version.h> // for PROTOCOL_VERSION

#include <map>
#include <random>


ConnectionManager::ConnectionManager(boost::asio::io_service &service, DownloadManager *parent)
    : m_shuttingDown(false),
    m_ioService(service),
    m_cronTimer(m_ioService),
    m_network(service),
    m_peerAddressDb(this),
    m_dlManager(parent)
{
    // The nonce is used in the status message to allow detection of connect-to-self.
    m_appNonce = (random() << 32) | random();

    std::map<int, std::string> table;
    table.insert(std::make_pair(Api::P2P::Version, "version"));
    table.insert(std::make_pair(Api::P2P::VersionAck, "verack"));
    table.insert(std::make_pair(Api::P2P::Ping, "ping"));
    table.insert(std::make_pair(Api::P2P::Pong, "pong"));
    table.insert(std::make_pair(Api::P2P::PreferHeaders, "sendheaders"));
    table.insert(std::make_pair(Api::P2P::GetHeaders, "getheaders"));
    table.insert(std::make_pair(Api::P2P::Headers, "headers"));
    table.insert(std::make_pair(Api::P2P::RejectData, "reject"));
    table.insert(std::make_pair(Api::P2P::Inventory, "inv"));
    table.insert(std::make_pair(Api::P2P::GetAddr, "getaddr"));
    table.insert(std::make_pair(Api::P2P::Addresses, "addr"));
    table.insert(std::make_pair(Api::P2P::Inventory, "inv"));
    table.insert(std::make_pair(Api::P2P::Data_Transaction, "tx"));
    table.insert(std::make_pair(Api::P2P::Data_MerkleBlock, "merkleblock"));
    table.insert(std::make_pair(Api::P2P::FilterLoad, "filterload"));
    table.insert(std::make_pair(Api::P2P::FilterClear, "filterclear"));
    table.insert(std::make_pair(Api::P2P::GetData, "getdata"));
    m_network.setMessageIdLookup(table);

    m_cronTimer.expires_from_now(boost::posix_time::seconds(20));
    m_cronTimer.async_wait(parent->strand().wrap(std::bind(&ConnectionManager::cron, this, std::placeholders::_1)));

    m_userAgent = "Flowee Super thingy";
}

void ConnectionManager::addInvMessage(const Message &message, int sourcePeerId)
{
    if (m_shuttingDown)
        return;
    m_dlManager->strand().post(std::bind(&DownloadManager::parseInvMessage, m_dlManager, message, sourcePeerId));
}

void ConnectionManager::addTransaction(const Tx &message, int sourcePeerId)
{
    if (m_shuttingDown)
        return;
    m_dlManager->strand().post(std::bind(&DownloadManager::parseTransaction, m_dlManager, message, sourcePeerId));
}

thread_local Streaming::BufferPool m_buffer;
Streaming::BufferPool &ConnectionManager::pool(int reserveSize)
{
    m_buffer.reserve(reserveSize);
    return m_buffer;
}

void ConnectionManager::connect(PeerAddress &address)
{
    if (m_shuttingDown)
        return;
    auto con = m_network.connection(address.peerAddress());
    std::unique_lock<std::mutex> lock(m_lock);
    // first check if we are already have a Peer for this endpoint
    if (m_peers.find(con.connectionId()) == m_peers.end()) {
        address.punishPeer(100); // when the connection succeeds, we remove the 100 again.
        con.setOnError(std::bind(&ConnectionManager::handleError, this, std::placeholders::_1, std::placeholders::_2));
        auto p = std::make_shared<Peer>(this, address);
        p->connect(std::move(con));
        m_peers.insert(std::make_pair(p->connectionId(), p));
    }
}

void ConnectionManager::disconnect(Peer *peer)
{
    assert(peer);
    if (m_shuttingDown)
        return;
    for (auto iface : m_dlManager->p2pNetListeners()) {
        iface->lostPeer(peer->connectionId());
    }
    std::unique_lock<std::mutex> lock(m_lock);
    assert(m_peers.find(peer->connectionId()) != m_peers.end());
    removePeer(peer);
}

uint64_t ConnectionManager::servicesBitfield() const
{
    return m_servicesBitfield;
}

void ConnectionManager::setServicesBitfield(const uint64_t &servicesBitfield)
{
    m_servicesBitfield = servicesBitfield;
}

int ConnectionManager::blockHeight() const
{
    return m_blockHeight;
}

void ConnectionManager::setBlockHeight(int blockHeight)
{
    m_blockHeight = blockHeight;
}

int ConnectionManager::blockHeightFor(const uint256 &blockId)
{
    return m_dlManager->blockchain().blockHeightFor(blockId);
}

uint256 ConnectionManager::blockHashFor(int height)
{
    return m_dlManager->blockchain().block(height).createHash();
}

uint64_t ConnectionManager::appNonce() const
{
    return m_appNonce;
}

void ConnectionManager::connectionEstablished(Peer *peer)
{
    if (m_shuttingDown)
        return;
    assert(peer);
    assert(peer->peerAddress().isValid());
    peer->peerAddress().punishPeer(-100); // this mirrors the 100 when we started connecting.
    peer->peerAddress().setServices(peer->services());

    std::unique_lock<std::mutex> lock(m_lock);
    // don't use if the client doesn't support any usable services.
    if (!peer->supplies_bloom() || !peer->supplies_network()) {
        logDebug() << "Node does not support bloom or network" << peer->connectionId() << peer->userAgent();
        removePeer(peer);
        return;
    }

    for (auto iface : m_dlManager->p2pNetListeners()) {
        iface->newPeer(peer->connectionId(), peer->userAgent(), peer->startHeight(),
                       peer->peerAddress());
    }
    auto peerIter = m_peers.find(peer->connectionId());
    assert(peerIter != m_peers.end());
    m_connectedPeers.insert(peer->connectionId());

    if (!peer->peerAddress().hasEverGotGoodHeaders()
            || time(nullptr) - peer->peerAddress().lastConnected() > 3600 * 36) {
        // check if this peer is using the same chain as us.
        requestHeaders(peerIter->second);
    }

    const auto previousSegment = peer->peerAddress().segment();
    if (previousSegment == 0) {
        std::map<PrivacySegment*, int> segmentUsage;
        for (auto ps : m_segments) {
            segmentUsage.insert(std::make_pair(ps, 0));
        }
        // now populate it with segment usage.
        const auto thisId = peer->connectionId();
        for (auto peerId : m_connectedPeers) {
            if (peerId != thisId) {
                auto i = m_peers.find(peerId);
                assert(i != m_peers.end());
                auto s = i->second->privacySegment();
                if (s) {
                    auto segmentIter = segmentUsage.find(s);
                    segmentIter->second++;
                }
            }
        }

        PrivacySegment *best = nullptr;
        int usageCount = 1000;
        for (auto s : segmentUsage) {
            if (s.second < usageCount) {
                usageCount = s.second;
                best = s.first;
            }
        }
        if (best) {
            peer->setPrivacySegment(best);
            peer->peerAddress().setSegment(best->segmentId());
        }
    } else {
        for (auto ps : m_segments) {
            if (ps->segmentId() == previousSegment) {
                peer->setPrivacySegment(ps);
                break;
            }
        }
    }
}

void ConnectionManager::addBlockHeaders(const Message &message, int sourcePeerId)
{
    if (m_shuttingDown)
        return;
    // TODO if downloadmanager triggered this (m_peerDownloadingHeaders)
    // then update metadata on the speed of this peer.
    m_dlManager->strand().post(std::bind(&Blockchain::processBlockHeaders,
                                         &m_dlManager->blockchain(), message, sourcePeerId));
}

void ConnectionManager::addAddresses(const Message &message, int sourcePeerId)
{
    if (m_shuttingDown)
        return;
    m_dlManager->strand().post(std::bind(&PeerAddressDB::processAddressMessage,
                                         &m_peerAddressDb, message, sourcePeerId));
}

void ConnectionManager::punish(std::shared_ptr<Peer> peer, int amount)
{
    assert(peer);
    if (m_shuttingDown)
        return;
    auto address = peer->peerAddress();
    short total = PUNISHMENT_MAX;
    short previous = total;
    if (address.isValid()) {
        previous = address.punishment();
        total = address.punishPeer(amount);

        for (auto iface : m_dlManager->p2pNetListeners()) {
            iface->punishMentChanged(peer->connectionId());
        }
    }
    if (total >= PUNISHMENT_MAX) { // too much punishment leads to a ban
        logInfo() << "Ban peer" << peer->connectionId() << previous << "=>" << total;
        for (auto iface : m_dlManager->p2pNetListeners()) {
            iface->lostPeer(peer->connectionId());
        }
        std::unique_lock<std::mutex> lock(m_lock);
        removePeer(peer.get());
    }
}

void ConnectionManager::punish(int connectionId, int amount)
{
    std::shared_ptr<Peer> p;
    {
        std::unique_lock<std::mutex> lock(m_lock);
        auto peerIter = m_peers.find(connectionId);
        if (peerIter == m_peers.end())
            return;
        p = peerIter->second;
    }
    punish(p, amount);
}

void ConnectionManager::requestHeaders(std::shared_ptr<Peer> peer)
{
    if (m_shuttingDown)
        return;
    Streaming::P2PBuilder builder(pool(4 + 32 * 10));
    builder.writeInt(PROTOCOL_VERSION);
    auto message = m_dlManager->blockchain().createGetHeadersRequest(builder);
    peer->sendMessage(message);
}

std::deque<std::shared_ptr<Peer>> ConnectionManager::connectedPeers() const
{
    std::unique_lock<std::mutex> lock(m_lock);
    std::deque<std::shared_ptr<Peer>> answer;
    if (m_shuttingDown)
        return answer;
    for (auto i : m_connectedPeers) {
        auto p = m_peers.find(i);
        assert(p != m_peers.end());
        answer.push_back(p->second);
    }
    return answer;
}

std::shared_ptr<Peer> ConnectionManager::peer(int connectionId) const
{
    std::unique_lock<std::mutex> lock(m_lock);
    auto i = m_peers.find(connectionId);
    if (i == m_peers.end())
        return nullptr;

    return i->second;
}

void ConnectionManager::addPrivacySegment(PrivacySegment *ps)
{
    assert(ps);
#ifndef NDEBUG
    // don't add it twice, please.
    for (auto s : m_segments) {
        assert (s != ps);
    }
#endif
    m_segments.push_back(ps);
}

void ConnectionManager::removePrivacySegment(PrivacySegment *ps)
{
    assert(ps);
    for (auto s = m_segments.begin(); s != m_segments.end(); ++s) {
        if (ps == *s) {
            m_segments.erase(s);
            break;
        }
    }
    return;
}

void ConnectionManager::setUserAgent(const std::string &userAgent)
{
    m_userAgent = userAgent;
}

int ConnectionManager::peerCount() const
{
    assert(m_peers.size() <= INT_MAX);
    return int(m_peers.size());
}


void ConnectionManager::cron(const boost::system::error_code &error)
{
    if (error)
        return;
    if (m_shuttingDown)
        return;
    m_cronTimer.expires_from_now(boost::posix_time::seconds(20));
    m_cronTimer.async_wait(m_dlManager->strand().wrap(std::bind(&ConnectionManager::cron, this, std::placeholders::_1)));

    logDebug() << "Cron";
    int now = static_cast<uint32_t>(time(nullptr));
    // check for connections that don't seem to connect.
    std::unique_lock<std::mutex> lock(m_lock);
    auto iter = m_peers.begin();
    while (iter != m_peers.end()) {
        Peer *peer = iter->second.get();
        logInfo() << "   " << iter->first << peer->userAgent() << (peer->status() == Peer::Connected ? "connected" : "connecting")
                             << "Wallet:" << (peer->privacySegment() ? peer->privacySegment()->segmentId() : 0);
        if ((peer->status() == Peer::Connecting || peer->status() == Peer::Connected)
                && peer->protocolVersion() == 0 && now - peer->connectTime() > 30) {
            // after 30 seconds of connects, give up.
            logInfo() << "   kicking dead connection" << peer->connectionId();
            iter = m_peers.erase(iter);
            peer->shutdown(); // shutdown takes ownership of peer and deletes it safely
        }
        else {
            ++iter;
        }
    }
}

void ConnectionManager::handleError(int remoteId, const boost::system::error_code &error)
{
    m_dlManager->strand().post(std::bind(&ConnectionManager::handleError_impl, this, remoteId, error));
}

void ConnectionManager::handleError_impl(int remoteId, const boost::system::error_code &error)
{
    logDebug() << "on error" << remoteId << error.message();
    if (m_shuttingDown)
        return;
    bool remove = false;
    int punish = 0;
    if (error ==  boost::asio::error::host_unreachable || error == boost::asio::error::network_unreachable) {
        remove = true;
        punish = 100; // likely ipv6 while we don't have that.
    }
    if (error ==  boost::asio::error::host_not_found) {
        remove = true;
        punish = 500; // faulty DNS name.
    }
    if (punish > 0) {
        std::unique_lock<std::mutex> lock(m_lock);
        auto iter = m_peers.find(remoteId);
        if (iter == m_peers.end())
            return;
        if (iter != m_peers.end()) {
            auto address = iter->second->peerAddress();
            if (address.isValid())
                address.punishPeer(punish);
        }
    }

    if (remove) {
        logDebug() << "removing" << remoteId;
        std::unique_lock<std::mutex> lock(m_lock);
        auto iter = m_peers.find(remoteId);
        if (iter == m_peers.end())
            removePeer(iter->second.get());
    }
}

void ConnectionManager::removePeer(Peer *p)
{
    const int id = p->connectionId();
    p->shutdown(); // shutdown takes ownership of peer and deletes it safely
    p = nullptr;

    auto i = m_connectedPeers.find(id);
    if (i != m_connectedPeers.end()) {
        m_connectedPeers.erase(i);
        m_dlManager->peerDisconnected(id);
    }

    auto iter = m_peers.find(id);
    assert (iter != m_peers.end());
    m_peers.erase(iter);
}

std::deque<PrivacySegment *> ConnectionManager::segments() const
{
    return m_segments;
}

void ConnectionManager::shutdown()
{
    std::unique_lock<std::mutex> lock(m_lock);
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    m_cronTimer.cancel();

    auto copy(m_peers);
    for (auto peer : copy) {
        removePeer(peer.second.get());
    }
    assert(m_peers.empty());

    // TODO m_peerAddressDb.save();
}
