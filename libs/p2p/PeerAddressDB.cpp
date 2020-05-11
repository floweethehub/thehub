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
#include <streaming/BufferPool.h>
#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>
#include <streaming/P2PParser.h>

enum SavingTags {
    Separator = 0,
    Hostname,
    IPAddress,
    Port,
    Services,
    LastConnected,
    Punishment,
    Segment,
    EverConnected,
    EverReceivedGoodHeaders
    // AskedAddr?
};

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

void PeerAddressDB::saveDatabase(const boost::filesystem::path &basedir)
{
    Streaming::BufferPool pool(m_peers.size() * 40);
    Streaming::MessageBuilder builder(pool);
    char ip[16];
    for (const auto &item : m_peers) {
        if (item.second.address.ipAddress.is_unspecified()) {
            builder.add(Hostname, item.second.address.hostname);
        } else {
            item.second.address.toAddr(ip);
            builder.addByteArray(IPAddress, ip, 16);
        }
        if (item.second.address.announcePort != 8333)
            builder.add(Port, item.second.address.announcePort);
        builder.add(Services, item.second.services);
        builder.add(LastConnected, uint64_t(item.second.lastConnected));
        if (item.second.punishment > 0)
            builder.add(Punishment, item.second.punishment);
        if (item.second.segment != 0)
            builder.add(Segment, item.second.segment);
        if (item.second.everConnected)
            builder.add(EverConnected, true);
        if (item.second.everReceivedGoodHeaders)
            builder.add(EverReceivedGoodHeaders, true);

        builder.add(Separator, true); // separator
    }
    auto data = builder.buffer();

    try {
        boost::filesystem::create_directories(basedir);
        boost::filesystem::remove(basedir / "peers.dat~");

        std::ofstream outFile((basedir / "peers.dat~").string());
        outFile.write(data.begin(), data.size());

        boost::filesystem::rename(basedir / "peers.dat~", basedir / "peers.dat");
    } catch (const std::exception &e) {
        logFatal() << "Failed to save the database. Reason:" << e.what();
    }
}

void PeerAddressDB::loadDatabase(const boost::filesystem::path &basedir)
{
    std::ifstream in((basedir / "peers.dat").string());
    if (!in.is_open())
        return;
    const auto dataSize = boost::filesystem::file_size(basedir / "peers.dat");
    Streaming::BufferPool pool(dataSize);
    in.read(pool.begin(), dataSize);
    Streaming::MessageParser parser(pool.commit(dataSize));
    PeerInfo info;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Separator) {
            if (info.address.isValid())
                insert(info);
            info = PeerInfo();
            info.everConnected = true; // defaults in saving that differ from struct defaults
            info.everReceivedGoodHeaders = true;
        }
        else if (parser.tag() == IPAddress) {
            info.address = EndPoint::fromAddr(parser.bytesData(), 8333);
        }
        else if (parser.tag() == Hostname) {
            info.address = EndPoint(parser.stringData(), 8333);
        }
        else if (parser.tag() == Port) {
            info.address.announcePort = info.address.peerPort = parser.intData();
        }
        else if (parser.tag() == Services) {
            info.services = parser.longData();
        }
        else if (parser.tag() == LastConnected) {
            info.lastConnected = parser.longData();
        }
        else if (parser.tag() == Punishment) {
            info.punishment = parser.intData();
        }
        else if (parser.tag() == Segment) {
            info.segment = parser.intData();
        }
        else if (parser.tag() == EverConnected) {
            info.everConnected = parser.boolData();
        }
        else if (parser.tag() == EverReceivedGoodHeaders) {
            info.everReceivedGoodHeaders = parser.boolData();
        }
    }
}

void PeerAddressDB::insert(PeerInfo &pi)
{
    if (pi.address.ipAddress.is_unspecified()) // try to see if hostname is an IP. If so, bypass DNS lookup
        try { pi.address.ipAddress = boost::asio::ip::address::from_string(pi.address.hostname); } catch (...) {}
    const bool hasIp = !pi.address.ipAddress.is_unspecified();
    if (!hasIp && pi.address.hostname.empty())
        return;
    for (auto i = m_peers.begin(); i != m_peers.end(); ++i) {
        if (hasIp && i->second.address.ipAddress == pi.address.ipAddress)
            return;
        if (!hasIp && i->second.address.hostname == pi.address.hostname)
            return;
    }

    m_peers.insert(std::make_pair(m_nextPeerId++, pi));
}
