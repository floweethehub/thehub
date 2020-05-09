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
#ifndef PEERADDRESSDB_H
#define PEERADDRESSDB_H

#include <NetworkEndPoint.h>

#include <map>

class PeerAddressDB;
class Message;
class ConnectionManager;

constexpr int PUNISHMENT_MAX = 1000;

class PeerAddress
{
public:
    const EndPoint &peerAddress() const;

    void successfullyConnected();
    void gotGoodHeaders();
    short punishPeer(short amount);
    short punishment() const;
    void resetPunishment();
    bool isValid() const;

    bool askedAddresses() const;
    void setAskedAddresses(bool on);
    bool hasEverConnected() const;
    bool hasEverGotGoodHeaders() const;

    uint16_t segment() const;
    void setSegment(uint16_t segment);

    void setInUse(bool on);

    void setServices(uint64_t services);

    uint32_t lastConnected() const;

    int id() const;

protected:
    friend class PeerAddressDB;
    explicit PeerAddress(PeerAddressDB *parent, int peerId);

private:
    PeerAddressDB *d;
    int m_id;
};

class PeerAddressDB
{
public:
    PeerAddressDB(ConnectionManager *parent);

    PeerAddress findBest(uint64_t requiredServices = 0, uint16_t segment = 0);

    int peerCount() const;

    void processAddressMessage(const Message &message, int sourcePeerId);

    void addOne(const EndPoint &endPoint);

    inline PeerAddress peer(int id) {
        assert(0 <= id);
        return PeerAddress(this, id);
    }

private:
    friend class PeerAddress;
    struct PeerInfo {
        EndPoint address;
        uint64_t services = 0;
        uint32_t lastConnected = 0;
        short punishment = 0;
        uint16_t segment = 0;
        short peerSpeed = 0;
        bool inUse = false;
        bool askedAddr = false;
        bool everConnected = false; // if false, lastConnected comes from untrusted peers
        bool everReceivedGoodHeaders = false;
    };
    void insert(PeerInfo &pi);

    std::map<int, PeerInfo> m_peers;
    int m_nextPeerId = 0;
    int m_disabledPeerCount = 0; // amount of peers with punishment >= 1000

    ConnectionManager *m_parent;
};

#endif
