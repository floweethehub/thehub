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
    logDebug() << "SyncSPVAction aiming for currentBlockHeight:" << currentBlockHeight;
    const auto now = boost::posix_time::microsec_clock::universal_time();
    uint32_t nowInSec = time(nullptr);

    std::map<PrivacySegment*, WalletInfo> wallets;
    /*
     * Privacy Segments are assigned to a number of peers, make an inventory of each segment.
     * For ease, realize that segments are the same thing as wallets here.
     */
    for (const auto &peer : m_dlm->connectionManager().connectedPeers()) {
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
    std::deque<PrivacySegment *> segments = m_dlm->connectionManager().segments();
    std::sort(segments.begin(), segments.end(), [](PrivacySegment *a, PrivacySegment *b){
        return a->priority() < b->priority();
    });
    const int unconnectedPeerCount = m_dlm->connectionManager().unconnectedPeerCount();
    for (auto segment : segments) {
        if (segment->firstBlock() == -1 || segment->firstBlock() > currentBlockHeight)
            continue;
        if (segment->priority() == PrivacySegment::OnlyManual)
            break;
        auto walletInfoIter = wallets.find(segment);
        size_t peers = 0;
        if (walletInfoIter != wallets.end())
            peers = walletInfoIter->second.peers.size();
        auto infoIter = m_segmentInfos.find(segment);
        if (infoIter == m_segmentInfos.end()) {
            m_segmentInfos.insert(std::make_pair(segment, Info()));
            infoIter = m_segmentInfos.find(segment);
        }
        assert(infoIter != m_segmentInfos.end());
        Info &info = infoIter->second;

        if (!info.previousDownloads.empty()) {
            PeerDownloadInfo &pdi = info.previousDownloads.back();
            if (pdi.toBlock == 0  // started download
                    && walletInfoIter != wallets.end() // have info
                    && walletInfoIter->second.downloading == nullptr) { // finished download

                // So it started a download and nobody is downloading anymore. Lets
                // update the PeerDownloadInfo with the last blockheight we downloaded.
                if (pdi.fromBlock == segment->backupSyncHeight() + 1)
                    pdi.toBlock = segment->lastBlockSynched();
                else
                    pdi.toBlock = segment->backupSyncHeight();

                if (pdi.fromBlock > pdi.toBlock) // download aborted
                    pdi.fromBlock = 0;
                logDebug() << "registring a download completed for"
                           << pdi.peerId << "from" << pdi.fromBlock << "to" << pdi.toBlock;
                assert(pdi.toBlock >= pdi.fromBlock);
            }
        }

        if (peers >= MIN_PEERS_PER_WALLET)
            continue;
        didSomething = true; // keep going, so we can wait for the peers to get ready.

        if (unconnectedPeerCount <= 2 || nowInSec - info.peersCreatedTime > 4) {
            // try to find new connections.
            while (peers < MIN_PEERS_PER_WALLET) {
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


    /*
     * So far we made sure that enough peers have been connected for each wallet,
     * we manage these peers actions here.
     *
     * This specifically means we ask them to download blocks for our wallet
     * and act on them being slow or similar issues.
     */

    for (auto w = wallets.begin(); w != wallets.end(); ++w) {
        PrivacySegment *privSegment = w->first;
        auto infoIter = m_segmentInfos.find(privSegment);
        if (infoIter == m_segmentInfos.end())  // wallet doesn't care about the current blockheight yet.
            continue;
        Info &info = infoIter->second;

        logDebug() << "WalletID:" << privSegment->segmentId()
                   << "origStart:" << privSegment->firstBlock()
                   << "lastBlockSynched:" << privSegment->lastBlockSynched()
                   << "backupSyncHeight:" << privSegment->backupSyncHeight();

        if (currentBlockHeight > privSegment->firstBlock()
                && (privSegment->lastBlockSynched() < currentBlockHeight
                    || privSegment->backupSyncHeight() < currentBlockHeight)) {
            didSomething = true;

            // is behind. Is someone downloading?
            if (w->second.downloading) {
                auto curPeer = w->second.downloading;
                // remember the downloader so we avoid asking the same peer to download the backup.

                // lets see if the peer is making progress.
                const int64_t timePassed = (now - info.lastCheckedTime).total_milliseconds();;
                const int32_t blocksDone = curPeer->lastReceivedMerkle() - info.lastHeight;
                logDebug() << "Downloading using peer" << curPeer->connectionId()
                           << "prevHeight:" << info.lastHeight
                           << "curHeight:" << curPeer->lastReceivedMerkle();

                if (timePassed > 4200) {
                    if (blocksDone < timePassed / 1000) {
                        // peer is stalling. I expect at least 1 block a second.
                        // we give them 2 segments try, or only one if they never started a single merkle dl
                        if (info.slowPunishment++ > 2 || curPeer->lastReceivedMerkle() == 0) {
                            logWarning() << "SyncSPV disconnects peer"
                                      << w->second.downloading->connectionId()
                                      << "that is stalling download of merkle-blocks";
                            m_dlm->connectionManager().punish(w->second.downloading, PUNISHMENT_MAX);
                            w->second.peers.erase(w->second.peers.find(w->second.downloading));
                            w->second.downloading = nullptr;
                            curPeer = nullptr;
                        }
                    } else if (blocksDone > 20) {
                        info.slowPunishment = 0;
                    }
                    if (curPeer) { // start new section every couple of seconds
                        info.lastHeight = curPeer->lastReceivedMerkle();
                        info.lastCheckedTime = now;
                    }
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
             *
             * On the Peer side we have the `int peerHeight() const`  method.
             */
            if (w->second.downloading == nullptr && !w->second.peers.empty()) {
                // logDebug() << "looking for a new downloader";
                int from;
                if (privSegment->backupSyncHeight() == privSegment->lastBlockSynched())
                    from = privSegment->lastBlockSynched();
                else
                    from = info.bloomPos;
                std::shared_ptr<Peer> preferred;
                for (auto p : w->second.peers) {
                    // logDebug() << "  + " << p->connectionId();
                    bool ok = true;
                    for (auto pdi : info.previousDownloads) {
                        if (pdi.peerId == p->connectionId()) {
                            if (from < pdi.toBlock) {// this one already downloaded for us
                                // logDebug() << "Skipping peer because it downloaded before";
                                ok = false;
                                break;
                            }
                        }
                        if (!ok)
                            break;
                    }
                    if (!ok)
                        continue;

                    if (from >= p->peerHeight()) {// peer isn't up-to-date yet
                        // logDebug() << "Skipping peer because its behind. Height:" << p->peerHeight();
                        continue;
                    }

                    if (!p->receivedHeaders()
                            // or we did that recently anyway.
                            && !(nowInSec - p->peerAddress().lastReceivedGoodHeaders() < 60 * 60 * 48)) {
                        // logDebug() << "Not picking peer because it didn't send us headers yet";
                        if (preferred == nullptr)
                            preferred = p;
                        continue;
                    }

                    // TODO use peer speed for preference
                    preferred = p;
                    break;
                }
                if (preferred) {
                    w->second.downloading = preferred;
                    logDebug() << "Wallet merkle-download started on peer" << preferred->connectionId()
                               << privSegment->backupSyncHeight()
                               << privSegment->lastBlockSynched();
                    if (privSegment->backupSyncHeight() == privSegment->lastBlockSynched()) {
                        logDebug() << "   [checkpoint]. Starting sync at" << privSegment->lastBlockSynched();
                        info.bloom = privSegment->bloomFilter();
                        info.bloomPos = privSegment->lastBlockSynched();
                    } else {
                        logDebug() << "   using bloom backup, restarting at" << from;
                        assert(info.bloom.flags() == BLOOM_UPDATE_ALL);
                        preferred->sendFilter(info.bloom, info.bloomPos);
                    }
                    preferred->startMerkleDownload(from + 1); // +1 because we start one after the last downloaded
                    info.previousDownloads.push_back({preferred->connectionId(), from + 1, 0});
                    info.lastHeight = from;
                    info.lastCheckedTime = now;
                }
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
