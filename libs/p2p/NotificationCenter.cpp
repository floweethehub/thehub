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
#include "NotificationCenter.h"

#include <cassert>

NotificationCenter::NotificationCenter()
{
}

void NotificationCenter::notifyNewBlock(int height)
{
    P2PNet::Notification n;
    n.blockHeight = height;
    size_t i = 0;
    std::unique_lock<std::mutex> lock(m_lock);
    if (m_listeners.empty())
        return;
    while (i < m_listeners.size()) {
        auto copy = m_listeners.at(i);
        copy->notifyNewBlock(n);
        if (m_listeners.at(i) == copy) // it didn't remove itself.
            ++i;
    }
}

void NotificationCenter::notifyNewTransaction(const P2PNet::Notification &n)
{
    size_t i = 0;
    std::unique_lock<std::mutex> lock(m_lock);
    if (m_listeners.empty())
        return;
    while (i < m_listeners.size()) {
        auto copy = m_listeners.at(i);
        copy->notifyNewTransaction(n);
        if (copy->isCollating())
            copy->updateSegment(n);

        if (m_listeners.at(i) == copy) // it didn't remove itself.
            ++i;
    }
}

void NotificationCenter::addListener(NotificationListener *nl)
{
    assert(nl);
    std::unique_lock<std::mutex> lock(m_lock);
    m_listeners.push_back(nl);
}

void NotificationCenter::removeListener(NotificationListener *nl)
{
    std::unique_lock<std::mutex> lock(m_lock);
    auto iter = m_listeners.begin();
    while (iter != m_listeners.end()) {
        if (*iter == nl)
            iter = m_listeners.erase(iter);
        else
            ++iter;
    }
}
