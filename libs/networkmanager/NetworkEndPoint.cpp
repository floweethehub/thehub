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
#include "NetworkEndPoint.h"

EndPoint EndPoint::fromAddr(const std::vector<char> &addr, int16_t port)
{
    assert(addr.size() == 16);
    // IPv4 is encoded by having the first 10 bytes as zero, then 2 bytes of 255
    bool isIPv4 = addr[10] == -1 && addr[11] == -1;
    for (int i = 0; isIPv4 && i < 10; ++i) {
        isIPv4 = addr[0] == 0;
    }

    if (isIPv4) {
        std::array<uint8_t,4> ipv4;
        for (int i = 0; i < 4; ++i)
            ipv4[i] = addr[i + 12];
        return EndPoint(boost::asio::ip::address_v4(ipv4), port);
    }

    std::array<uint8_t,16> ipv6;
    for (int i = 0; i < 16; ++i)
        ipv6[i] = addr[i];

    return EndPoint(boost::asio::ip::address_v6(ipv6), port);
}
