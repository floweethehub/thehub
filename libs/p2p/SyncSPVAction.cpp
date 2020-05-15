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
#include "SyncSPVAction.h"
#include "PrivacySegment.h"
#include "DownloadManager.h"
#include "Peer.h"

#include <set>

constexpr int MIN_PEERS_PER_WALLET = 3;

SyncSPVAction::SyncSPVAction(DownloadManager *parent)
    : Action(parent)
{
}

struct WalletInfo {
    std::shared_ptr<Peer> downloading;
    std::set<std::shared_ptr<Peer>> peers;
};

void SyncSPVAction::execute(const boost::system::error_code &error)
{
    if (error)
        return;

    const int currentBlockHeight = m_dlm->blockHeight();
    const auto now = boost::posix_time::microsec_clock::universal_time();
    uint32_t nowInSec = time(nullptr);

    std::map<PrivacySegment*, WalletInfo> wallets;
    /*
     * Privacy Segments are assigned to a number of peers, make an inventory of each segment.
     * For ease, segments are the same thing as wallets here.
     */
    for (auto peer : m_dlm->connectionManager().connectedPeers()) {
        auto *ps = peer->privacySegment();
        if (ps) {
            auto iter = wallets.find(ps);
            if (iter == wallets.end()) {
                WalletInfo info;
                info.peers.insert(peer);
                if (peer->merkleDownloadInProgress())
                    info.downloading = peer;
                wallets.insert(std::make_pair(ps, info));
            }
            else {
                if (iter->second.downloading == nullptr && peer->merkleDownloadInProgress())
                    iter->second.downloading = peer;
                iter->second.peers.insert(peer);
            }
        }
    }

    bool didSomething = false;

    // connect to enough peers for each wallet.
    for (auto segment : m_dlm->connectionManager().segments()) {
        if (segment->firstBlock() == -1 || segment->firstBlock() > currentBlockHeight)
            continue;
        auto i = wallets.find(segment);
        size_t peers = 0;
        if (i != wallets.end())
            peers = i->second.peers.size();
        auto infoIter = m_segmentInfos.find(segment);
        if (infoIter == m_segmentInfos.end()) {
            m_segmentInfos.insert(std::make_pair(segment, Info()));
            infoIter = m_segmentInfos.find(segment);
        }
        if (peers >= MIN_PEERS_PER_WALLET)
            continue;

        didSomething = true; // keep going, so we can wait for the peers to get ready.

        assert(infoIter != m_segmentInfos.end());
        Info &info = infoIter->second;
        if (nowInSec - info.peersCreatedTime > 30) {
            // try to find new connections.
            while (peers < MIN_PEERS_PER_WALLET) { // do we want to make minimum peers configurable?
                auto address = m_dlm->connectionManager().peerAddressDb()
                        .findBest(/*network and bloom*/ 1 | 4, segment->segmentId());
                if (!address.isValid())
                    break;

                logInfo() << "creating a new connection for PrivacySegment" << segment->segmentId();
                m_dlm->connectionManager().connect(address);
                peers++;
                info.peersCreatedTime = nowInSec;
            }
        }
    }

    for (auto w = wallets.begin(); w != wallets.end(); ++w) {
        PrivacySegment *privSegment = w->first;
        auto infoIter = m_segmentInfos.find(privSegment);
        if (infoIter == m_segmentInfos.end())
            continue;
        Info &info = infoIter->second;

        if (currentBlockHeight > privSegment->firstBlock()
                && (privSegment->lastBlockSynched() < currentBlockHeight
                    || privSegment->backupSyncHeight() < currentBlockHeight)) {
            didSomething = true;

            // is behind. Is someone downloading?
            if (w->second.downloading) {
                auto cur = w->second.downloading;
                // remember the downloader so we avoid asking the same peer to download twice.
                info.previousDownloadedBy.insert(cur->connectionId());

                // lets see if the peer is making progress.
                const uint32_t timePassed = (now - info.lastCheckedTime).total_milliseconds();;
                const uint32_t blocksDone = cur->lastReceivedMerkle() - info.lastHeight;
                if (blocksDone < timePassed * 3 / 1000) {
                    // peer is stalling. I expect at least 3 blocks a second.
                    if (info.slowPunishment++ > 3) {
                        logInfo() << "SyncSPV disconnects peer that is stalling download of merkle-blocks"
                                  << w->second.downloading->connectionId();
                        m_dlm->connectionManager().punish(w->second.downloading, PUNISHMENT_MAX);
                        w->second.peers.erase(w->second.peers.find(w->second.downloading));
                        w->second.downloading = nullptr;
                    }
                } else if (blocksDone > 20) {
                    info.slowPunishment = 0;
                }
            }

            /*
             * lets assign a downloader.
             * A 'wallet' (aka PrivacySegment) needs at least one pass by a peer to download all
             * merkle blocks. We do this by picking a peer and calling startMerkleDownload() on it.
             *
             * This code also takes into consideration the fact that we should not trust
             * a single random node on the Internet. So when we finished asking one peer
             * we go and assign a second peer to be our backup. With probably the same
             * result, but maybe with more transactions.
             *
             * The PrivateSegment has two getters for this:
             *
             *  /// returns the last block that was synched
             *  int lastBlockSynched() const;
             *  /// a backup peer doing a second sync has reached this height
             *  int backupSyncHeight() const;
             */
            if (w->second.downloading == nullptr && !w->second.peers.empty()) {
                std::shared_ptr<Peer> preferred;
                for (auto p : w->second.peers) {
                    if (info.previousDownloadedBy.find(p->connectionId()) != info.previousDownloadedBy.end())
                        continue;
                    if (p->lastReceivedMerkle() == 0)
                        preferred = p;
                    if (privSegment->lastBlockSynched() == p->lastReceivedMerkle()) {
                        // then this is going to be downloader for now.
                        preferred = p;
                        break;
                    }
                }
                if (preferred) {
                    w->second.downloading = preferred;
                    logDebug() << "Wallet merkle-download started on peer" << preferred->connectionId()
                               << privSegment->lastBlockSynched()
                               << privSegment->backupSyncHeight();
                    int from = privSegment->lastBlockSynched() + 1;// +1 because we start one after the last download
                    if (privSegment->backupSyncHeight() == privSegment->lastBlockSynched()) {
                        info.bloom = privSegment->bloomFilter();
                        info.bloomPos = privSegment->backupSyncHeight() + 1;
                    } else {
                        from = info.bloomPos;
                        preferred->sendFilter(info.bloom, info.bloomPos);
                        if (from + 1 != privSegment->backupSyncHeight())
                            logDebug() << "Re-re-requesting some blocks. From" << from
                                       << "to" << privSegment->backupSyncHeight();
                    }
                    preferred->startMerkleDownload(from);
                    info.previousDownloadedBy.insert(preferred->connectionId());
                }
            }

            if (w->second.downloading) {
                info.lastHeight = w->second.downloading->lastReceivedMerkle();
                info.lastCheckedTime = now;
            }
        }
    }

    if (didSomething) {
        m_quietCount = 0;
    } else if (m_quietCount++ > 2) {
        logInfo() << "SyncSPVAction done";
        m_dlm->done(this);
        return;
    }
    again();
}
