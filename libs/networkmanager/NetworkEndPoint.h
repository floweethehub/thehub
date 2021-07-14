/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2021 Tom Zander <tom@flowee.org>
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
#ifndef NETWORKENDPOINT_H
#define NETWORKENDPOINT_H

#include <string>
#include <cstdint>
#include <Logger.h>

#include <boost/asio/ip/address.hpp>

/// Describes a remote server.
struct EndPoint
{
    /// Invalid endpoint
    EndPoint()
        : peerPort(0),
        announcePort(0)
    {
    }
    /**
     * Hostname constructor
     * Port will be used for both peer and announce port members.
     */
    EndPoint(const std::string &hostname, std::uint16_t port)
        : hostname(hostname),
          peerPort(port),
          announcePort(port)
    {
    }
    /**
     * IP address constructor.
     * Port will be used for both peer and announce port members.
     */
    EndPoint(const boost::asio::ip::address &ip, std::uint16_t port)
        : ipAddress(ip),
          peerPort(port),
          announcePort(port)
    {
    }

    bool isValid() const { return announcePort > 0 && (!hostname.empty() || !ipAddress.is_unspecified()); }

    /// Implement the P2P format 'addr' of a 16-byte vector encoding the address
    static EndPoint fromAddr(const std::vector<char> &addr, int16_t port);

    /// write address to a bytearray of 16 bytes.
    void toAddr(char *addr) const;

    boost::asio::ip::address ipAddress;
    std::string hostname;
    std::uint16_t peerPort = 0;
    std::uint16_t announcePort = 0;
    /// The connection id refers to the NetworkConnection id, as used by the NetworkManager
    int connectionId = -1;
};

inline Log::Item operator<<(Log::Item item, const EndPoint &ep) {
    if (item.isEnabled()) {
        const bool old = item.useSpace();
        item.nospace() << "EndPoint(";
        if (ep.ipAddress.is_unspecified()) {
            item << ep.hostname;
        }
        else {
            if (ep.ipAddress.is_v6())
                item << "[";
            item << ep.ipAddress.to_string().c_str();
            if (ep.ipAddress.is_v6())
                item << "]";
        }
        item << ':' << ep.announcePort;
        if (ep.announcePort != ep.peerPort && ep.peerPort != 0)
            item << '|' << ep.peerPort;
        item << ')';
        if (old)
            return item.space();
    }
    return item;
}
inline Log::SilentItem operator<<(Log::SilentItem item, const EndPoint&) { return item; }

#endif
