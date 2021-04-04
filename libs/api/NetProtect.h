/*
 * This file is part of the Flowee project
 * Copyright (C) 2021 Tom Zander <tom@flowee.org>
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
#ifndef NETPROTECT_H
#define NETPROTECT_H

#include <vector>
#include <mutex>

#include <boost/asio/ip/address.hpp>

class NetworkConnection;

class NetProtect
{
public:
    /// create a new NetProtect
    /// @param maxHosts is the maximum amount of hosts we serve at any time.
    NetProtect(int maxHosts);

    bool shouldAccept(const NetworkConnection &connection,
            uint32_t connectionTime);

    void addWhitelistedAddress(boost::asio::ip::address ipAddress);

private:
    int m_maxHosts;
    struct Connect {
        boost::asio::ip::address ipAddress;
        uint32_t connectionTime;
    };
    std::vector<Connect> m_log;
    std::vector<boost::asio::ip::address> m_whitelist;
    mutable std::mutex m_lock;
};

#endif
