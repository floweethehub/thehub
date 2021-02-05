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
#ifndef NOTIFICATIONCENTER_H
#define NOTIFICATIONCENTER_H

#include "NotificationListener.h"
#include <cstdint>
#include <vector>
#include <mutex>

/**
 * Notifications are sent here from the p2pnet lib for users to subscribe to.
 */
class NotificationCenter
{
public:
    NotificationCenter();

    void notifyNewBlock(int height);
    void notifyNewTransaction(const P2PNet::Notification & notification);

    void addListener(NotificationListener *nl);
    void removeListener(NotificationListener *nl);

private:
    std::vector<NotificationListener*> m_listeners;
    mutable std::mutex m_lock;
};

#endif
