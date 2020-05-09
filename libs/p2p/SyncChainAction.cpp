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
#include "SyncChainAction.h"
#include "DownloadManager.h"
#include "Peer.h"

#include <Logger.h>

#include <time.h>

constexpr int MinGoodPeers = 4;
constexpr int MaxGoodPeers = 8;
constexpr int RINGBUF_SIZE = 10;

SyncChainAction::SyncChainAction(DownloadManager *parent)
    : Action(parent)
{
    m_startHeight = parent->blockHeight();
    m_states.resize(RINGBUF_SIZE);
}

void SyncChainAction::execute(const boost::system::error_code &error)
{
    if (error)
        return;

    /*
     * This class observes the system, typically shortly after startup.
     * The point is to make sure we have a fully up-to-date chain.
     * -----------------------------------------------------------
     *
     * The goal is to get various peers to agree on what is the 'tip'.
     * These peers should also come from different sections of the Internet
     * since most of the sybils would end up with very similar IPs. (TODO)
     *
     * We require each of those peers to have done a 'getheaders' call as the
     * system will disconnect any peer that doesn't agree with the longest chain
     * and our checkpoints.
     *
     * Last, we use the date to guestimate the chain-height so we know we are
     * stuck on a dead couple of nodes if stay get behind too much.
     */
    if (m_dlm->peerDownloadingHeaders() != -1) { // a download is in progress
        // no need to do anything, just wait.

        // We will check if we are actually progressing up in block-height since
        // our peer might stop sending us headers for some reason.
        const uint32_t now = time(nullptr);

        int score = 0;
        int prevHeight = 0;
        uint32_t oldestTime = 0;
        for (int i = m_stateIndex + 1; i != m_stateIndex; ++i) {
            if (prevHeight == 0) {
                if (i < RINGBUF_SIZE) {
                    prevHeight = m_states[i].height;
                    if (prevHeight == 0) // when no measurements present, allow starting
                        score += 100;
                    else
                        oldestTime = m_states[i].timestamp;
                }
            } else {
                int diff = m_states[i].height - prevHeight;
                if (diff > 900)
                    score += 100;
            }
            if (i >= RINGBUF_SIZE - 1) i = -1; // behave like a ring-buffer.
        }
        if (score < 400) {
            // getting maybe 3000 block-headers in 15 secs is too slow :(

            assert(oldestTime > 0);
            if (now - oldestTime > 60) {
                // this action should run every 1.5 seconds, it 10 measurements took
                // more than a minute then we just slept or something.
                logDebug() << "Slowness detected in header download, probably due to app-sleep. Waiting longer";
            }
            else {
                // take action, find a different peer to download from.
                auto &cm = m_dlm->connectionManager();
                if (cm.connectedPeers().size() > 1) { // only if we actually have another
                    logInfo() << "SyncChain disconnects peer that is holding up downloads" << m_dlm->peerDownloadingHeaders();
                    auto p = cm.peer(m_dlm->peerDownloadingHeaders());
                    if (p)
                        cm.disconnect(p);
                } else if (canAddNewPeer()){
                    logInfo() << "SyncChain would like a faster peer. Connecting to new one";
                    connectToNextPeer();
                }
            }
        }
        m_states[m_stateIndex].height = m_dlm->blockHeight();
        m_states[m_stateIndex++].timestamp = now;
        if (m_stateIndex >= RINGBUF_SIZE)
            m_stateIndex = 0;

        again();
        return;
    }

    std::set<int> existingPeersToAsk;
    int goodPeers = 0;
    for (auto peer: m_dlm->connectionManager().connectedPeers()) {
        if (peer->receivedHeaders()
                // or we did that recently anyway.
                || (peer->peerAddress().hasEverGotGoodHeaders() && peer->peerAddress().punishment() <= 300)) {
            auto i = m_doubtfulPeers.find(peer->connectionId());
            if (i != m_doubtfulPeers.end())
                m_doubtfulPeers.erase(i);

            goodPeers++;
            if (goodPeers >= MaxGoodPeers)
                break;
        }
        else if (peer->startHeight() > m_dlm->blockHeight()) {
            auto i = m_doubtfulPeers.find(peer->connectionId());
            if (i == m_doubtfulPeers.end()) {
                // new peer. it looks promising
                // But lets wait to see if the headers call will be sent.
                m_doubtfulPeers.insert(std::make_pair(peer->connectionId(), time(nullptr)));
            }
            else if (time(nullptr) - i->second > 10) { // previously seen as new.
                existingPeersToAsk.insert(peer->connectionId());
            }
        }
    }

    if (goodPeers < MinGoodPeers) {
        logInfo() << "SyncChain has" << goodPeers << "good peers, which is less than I need. Connecting a new peer";
        connectToNextPeer();
    }
    // so we have enough peers, that seem to agree with our chain. Lets see if our chain
    // is up-to-date.
    else if (m_dlm->blockchain().expectedBlockHeight() - m_dlm->blockHeight() < 3) {
        // close enough. We are not catching-up, anyway.
        logDebug() << "SyncChain done";
        m_dlm->done(this);
        return;
    }
    else if (!existingPeersToAsk.empty()) {
        auto peer = m_dlm->connectionManager().peer(*existingPeersToAsk.begin());
        logDebug() << "SyncChain requests headers from" << peer->connectionId();
        peer->sendMessage(Message(Api::LegacyP2P, Api::P2P::GetHeaders));
    }
    else {
        // ok, this is annoying.
        // we have enough peers, they have all had the headers sent, we are not downloading any headers.
        // but we seem to be behind...

        // Lets add a couple more peers.
        if (goodPeers < MaxGoodPeers) {
            logDebug() << "SyncChain has" << goodPeers << "good peers, but we are still behind. Connecting a new peer";
            connectToNextPeer();
        }
    }
    again();
}

void SyncChainAction::connectToNextPeer()
{
    auto address = m_dlm->connectionManager().peerAddressDb().findBest(5);
    if (address.isValid())
        m_dlm->connectionManager().connect(address);

    m_lastPeerAddedTime = time(nullptr);
}

bool SyncChainAction::canAddNewPeer()
{
    return time(nullptr) - m_lastPeerAddedTime > 30;
}
