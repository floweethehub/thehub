/*
 * This file is part of the Flowee project
 * Copyright (C) 2020 Tom Zander <tom@flowee.org>
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
#ifndef SYNCCHAINACTION_H
#define SYNCCHAINACTION_H

#include "Action.h"

#include <set>

class SyncChainAction : public Action
{
public:
    SyncChainAction(DownloadManager *parent);

    void execute(const boost::system::error_code &error) override;

private:
    void connectToNextPeer();
    bool canAddNewPeer();

    struct DownloadState {
        uint32_t timestamp = 0;
        int height = 0;
    };

    std::vector<DownloadState> m_states; // acts as a ring-buffer
    int m_stateIndex = 0;

    int m_startHeight;
    std::map<int, int64_t> m_doubtfulPeers;
    uint32_t m_lastPeerAddedTime = 0;
};

#endif
