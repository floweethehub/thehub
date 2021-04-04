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

#include "NetProtect.h"
#include "NetworkConnection.h"

NetProtect::NetProtect(int maxHosts)
    : m_maxHosts(maxHosts)
{
    m_log.reserve(m_maxHosts * 4);
}

bool NetProtect::shouldAccept(const NetworkConnection &connection, uint32_t connectionTime)
{
    EndPoint ep = connection.endPoint();
    assert(!ep.ipAddress.is_unspecified()); // lets assume a cetain usage. Incoming named hosts is not supported (or likely)
    if (ep.ipAddress.is_loopback())
        return true;
    for (const auto &ip : m_whitelist) {
        if (ip == ep.ipAddress)
            return true;
    }

    std::unique_lock<std::mutex> lock(m_lock);
    int tier1, tier2, tier3, tier4;
    tier1 = tier2 = tier3 = 0;
    // iterate backwards. Newest are at the end
    for (int i = static_cast<int>(m_log.size()) - 1; i >= 0; --i) {
        const Connect &c = m_log.at(i);
        const int diff = connectionTime - c.connectionTime;
        assert(diff >= 0);
        if (diff > 300) {
            if (i > 20) {
                // cut off, remove all old ones
                for (int x = 0; x <= i; ++x) {
                    m_log.erase(m_log.begin());
                }
            }
            break; // stop counting
        }
        if (c.ipAddress == ep.ipAddress) {
            if (diff < 10)
                ++tier1;
            else if (diff < 30)
                ++tier2;
            else if (diff < 90)
                ++tier3;
        }
    }

    bool ok = true;
    // determine if the connects are coming in too fast, then we say "no".
    if (tier1 >= 1) {// slow down bro
        ok = tier1 == 1 && tier2 <= 1 && tier3 <= 2; // remember, tier-ints are not cumulative
    }
    if (ok)
        m_log.push_back({ep.ipAddress, connectionTime});
    return ok;
}

void NetProtect::addWhitelistedAddress(boost::asio::ip::address ipAddress)
{
    m_whitelist.push_back(ipAddress);
}
