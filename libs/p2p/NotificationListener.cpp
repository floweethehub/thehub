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
#include "NotificationListener.h"

NotificationListener::NotificationListener()
{
}

NotificationListener::~NotificationListener()
{
    // empty
}

void NotificationListener::notifyNewBlock(const P2PNet::Notification&)
{
    // empty
}

void NotificationListener::notifyNewTransaction(const P2PNet::Notification&)
{
    // empty
}

void NotificationListener::segmentUpdated(const P2PNet::Notification&)
{
    // empty
}

void NotificationListener::updateSegment(const P2PNet::Notification &notification)
{
    std::unique_lock<std::mutex> lock(m_lock);
    if (m_doCollate == false)
        return;

    bool found = false;
    // find and update segment
    P2PNet::Notification group;
    for (auto &iter : m_collatedData) {
        if (iter.privacySegment == notification.privacySegment) {
            iter.deposited += notification.deposited;
            iter.spent += notification.spent;
            ++iter.txCount;
            group = iter;
            found = true;
            break;
        }
    }
    if (!found) {
        m_collatedData.push_back(notification);
        group = notification;
    }

    segmentUpdated(group);
}

void NotificationListener::setCollation(bool on)
{
    m_doCollate = on;
    if (!on)
        flushCollate();
}


