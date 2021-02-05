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
#ifndef NOTIFICATIONLISTENER_H
#define NOTIFICATIONLISTENER_H

#include <cstdint>
#include <vector>
#include <mutex>

namespace P2PNet
{
struct Notification
{
    int blockHeight = -1;
    int txCount = 0;
    int privacySegment = -1;
    int64_t deposited = 0;
    int64_t spent = 0;
};
}

class NotificationListener
{
public:
    NotificationListener();
    virtual ~NotificationListener();

    virtual void notifyNewBlock(const P2PNet::Notification &notification);
    virtual void notifyNewTransaction(const P2PNet::Notification &notification);
    virtual void segmentUpdated(const P2PNet::Notification &notification);

    void updateSegment(const P2PNet::Notification &notification);

    void setCollation(bool on);
    inline bool isCollating() const {
        return m_doCollate;
    }
    inline void flushCollate() {
        m_collatedData.clear();
    }
    inline std::vector<P2PNet::Notification> collatedData() const {
        return m_collatedData;
    }

private:
    bool m_doCollate = false;
    std::vector<P2PNet::Notification> m_collatedData;
    mutable std::mutex m_lock;
};

#endif
