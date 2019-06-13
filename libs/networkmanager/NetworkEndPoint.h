/*
 * This file is part of the Flowee project
 * Copyright (C) 2016, 2019 Tom Zander <tomz@freedommail.ch>
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
    EndPoint()
        : peerPort(0),
        announcePort(0)
    {
    }
    EndPoint(const std::string &hostname, std::uint16_t port)
        : hostname(hostname),
          peerPort(port),
          announcePort(port)
    {
    }
    EndPoint(const boost::asio::ip::address &ip, std::uint16_t port)
        : ipAddress(ip),
          peerPort(port),
          announcePort(port)
    {
    }

    bool isValid() const { return announcePort > 0 && (!hostname.empty() || !ipAddress.is_unspecified()); }

    boost::asio::ip::address ipAddress;
    std::string hostname;
    std::uint16_t peerPort = 0;
    std::uint16_t announcePort = 0;
    int connectionId = -1;
};

inline Log::Item operator<<(Log::Item item, const EndPoint &ep) {
    if (item.isEnabled()) {
        const bool old = item.useSpace();
        item.nospace() << "EndPoint[";
        if (ep.ipAddress.is_unspecified())
            item << ep.hostname;
        else
            item << ep.ipAddress.to_string();
        item << ':' << ep.announcePort;
        if (ep.announcePort != ep.peerPort && ep.peerPort != 0)
            item << '|' << ep.peerPort;
        item << ']';
        if (old)
            return item.space();
    }
    return item;
}
inline Log::SilentItem operator<<(Log::SilentItem item, const EndPoint&) { return item; }

#endif
