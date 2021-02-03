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
#include "CleanPeersAction.h"
#include "DownloadManager.h"
#include "Peer.h"

#include <Logger.h>

CleanPeersAction::CleanPeersAction(DownloadManager *parent)
    : Action(parent)
{
    setInterval(35 * 1000);
}

void CleanPeersAction::execute(const boost::system::error_code &error)
{
    if (error)
        return;

    for (auto peer : m_dlm->connectionManager().connectedPeers()) {
        logDebug() << "peer" << peer->connectionId() << "headers" << peer->peerAddress().lastReceivedGoodHeaders()
                   << time(nullptr) - peer->peerAddress().lastConnected() << "s";
        // ban peers that never responded to our request for headers
        if (peer->peerAddress().lastReceivedGoodHeaders() == 0
                && time(nullptr) - peer->peerAddress().lastConnected() > 90) {
            m_dlm->connectionManager().punish(peer, PUNISHMENT_MAX);
            continue;
        }
    }

    again();
}
