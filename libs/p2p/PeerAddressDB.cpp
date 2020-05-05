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
#include "PeerAddressDB.h"
#include "ConnectionManager.h"
#include <Message.h>
#include <streaming/P2PParser.h>

const EndPoint &PeerAddress::peerAddress() const
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    return i->second.address;
}

void PeerAddress::successfullyConnected()
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    time_t now = time(NULL);
    assert(now > 0);
    i->second.lastConnected = static_cast<uint32_t>(now);
    if (i->second.punishment > 500)
        i->second.punishment -= 125;
    i->second.inUse = true;
    i->second.everConnected = true;
}

void PeerAddress::gotGoodHeaders()
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    time_t now = time(NULL);
    assert(now > 0);
    i->second.lastConnected = static_cast<uint32_t>(now);
    if (i->second.punishment > 500)
        i->second.punishment -= 200;
    i->second.everReceivedGoodHeaders = true;
}

short PeerAddress::punishPeer(short amount)
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    int newPunishment = int(i->second.punishment) + amount;
    if (i->second.punishment < PUNISHMENT_MAX && newPunishment >= PUNISHMENT_MAX) {
        d->m_disabledPeerCount++;
    } else if (i->second.punishment >= PUNISHMENT_MAX && newPunishment < PUNISHMENT_MAX) {
        d->m_disabledPeerCount--;
    }

    i->second.punishment = std::min(newPunishment, 0xeFFF); // avoid overflow
    return i->second.punishment;
}

short PeerAddress::punishment() const
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    return i->second.punishment;
}

void PeerAddress::resetPunishment()
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    i->second.punishment = 0;
}

bool PeerAddress::isValid() const
{
    return d && m_id >= 0 && d->m_nextPeerId > m_id;
}

bool PeerAddress::askedAddresses() const
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    return i->second.askedAddr;
}

void PeerAddress::setAskedAddresses(bool on)
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    i->second.askedAddr = on;
}

bool PeerAddress::hasEverConnected() const
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    return i->second.everConnected;
}

bool PeerAddress::hasEverGotGoodHeaders() const
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    return i->second.everReceivedGoodHeaders;
}

uint16_t PeerAddress::segment() const
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    return i->second.segment;
}

void PeerAddress::setSegment(uint16_t segment)
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    i->second.segment = segment;
}

void PeerAddress::setInUse(bool on)
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    i->second.inUse = on;
}

void PeerAddress::setServices(uint64_t services)
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    i->second.services = services;
}

uint32_t PeerAddress::lastConnected() const
{
    auto i = d->m_peers.find(m_id);
    assert(d->m_peers.end() != i);
    return i->second.lastConnected;
}

PeerAddress::PeerAddress(PeerAddressDB *parent, int peerId)
    : d(parent), m_id(peerId)
{
    assert(parent);
}

int PeerAddress::id() const
{
    return m_id;
}


// ////////////////////////////////////////

PeerAddressDB::PeerAddressDB(ConnectionManager *parent)
    : m_parent(parent)
{
}

PeerAddress PeerAddressDB::findBest(uint64_t requiredServices, uint16_t segment)
{
    if (m_nextPeerId == 0)
        return PeerAddress(this, -1);
    std::array<int, 10> good;
    int goodIndex = 0;
    for (int attempts = 0; attempts < 500 && goodIndex < 10; ++attempts) {
        const int i = rand() % m_nextPeerId;
        PeerInfo &info = m_peers[i];
        if (!info.inUse && (info.services & requiredServices) == requiredServices
                && info.punishment < PUNISHMENT_MAX
                && (segment == 0 || segment == info.segment || info.segment == 0)
                && info.address.ipAddress.is_v4()) { // TODO detect network availability instead.
            good[goodIndex++] = i;
        }
    }

    if (goodIndex == 0) // nothing found
        return PeerAddress(this, -1);

    int best = 0;
    int bestScore = 0;
    for (int i = 0; i < goodIndex; ++i) {
        int score = 0;
        PeerInfo &info = m_peers[good[i]];
        score = PUNISHMENT_MAX - info.punishment;

        int hoursAgoConnected = (time(nullptr) - info.lastConnected) / 3600;
        if (info.everConnected) // improve score
            hoursAgoConnected /= 2;
        score += 1000 - std::min(1000, hoursAgoConnected);

        if (info.address.announcePort == 8333) // prefer default port
            score += 500;

        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return PeerAddress(this, good[best]);
}

int PeerAddressDB::peerCount() const
{
    return static_cast<int>(m_peers.size()) - m_disabledPeerCount;
}

void PeerAddressDB::processAddressMessage(const Message &message, int sourcePeerId)
{
    size_t oldCount = m_peers.size();
    try {
        Streaming::P2PParser parser(message);
        const size_t count = parser.readCompactInt();
        logDebug() << "Received" << count << "addresses" << "from" << sourcePeerId;
        for (size_t i = 0; i < count; ++i) {
            PeerInfo info;
            info.lastConnected = parser.readInt();
            info.services = parser.readLong();
            auto ip = parser.readBytes(16);
            auto port = parser.readWordBE();
            info.address = EndPoint::fromAddr(ip, port);
            insert(info);
        }
    } catch (const std::runtime_error &err) {
        logInfo() << "Failed to read address message from peer:" << sourcePeerId;
        m_parent->punish(sourcePeerId);
        return;
    }
    if (oldCount != m_peers.size())
        logInfo().nospace() << "We now have " << m_peers.size() << " addresses (thanks! peer:" << sourcePeerId << ")";
}

void PeerAddressDB::addOne(const EndPoint &endPoint)
{
    PeerInfo info;
    info.address = endPoint;
    info.services = 5;
    insert(info);
}

void PeerAddressDB::insert(const PeerAddressDB::PeerInfo &pi)
{
    for (auto i = m_peers.begin(); i != m_peers.end(); ++i) {
        if (i->second.address.ipAddress == pi.address.ipAddress)
            return;
    }

    m_peers.insert(std::make_pair(m_nextPeerId++, pi));
}
