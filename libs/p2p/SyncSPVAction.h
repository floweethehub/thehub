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
#ifndef SYNCSPVACTION_H
#define SYNCSPVACTION_H

#include "Action.h"

#include <bloom.h>
#include <map>
#include <set>
#include <boost/date_time/posix_time/posix_time.hpp>

class PrivacySegment;

class SyncSPVAction : public Action
{
public:
    SyncSPVAction(DownloadManager *parent);

protected:
    void execute(const boost::system::error_code &error) override;

private:
    int m_quietCount = 0;

    struct PeerDownloadInfo {
        int peerId;
        int fromBlock;
        int toBlock; // to-and-including
    };

    struct Info {
        boost::posix_time::ptime lastCheckedTime;
        uint32_t peersCreatedTime;
        int lastHeight = 0;
        int slowPunishment = 0; // punishment score for being slow

        /**
         * Remember which blocks were downloaded by which peer in order to actually
         * get our security that when we ask multiple peers for the same blocks, we
         * don't mess up and ask the same one twice.
         */
        std::vector<PeerDownloadInfo> previousDownloads;

        // for the second peer we ask to download we make a backup of the bloom as it was
        // at the start of the run.
        CBloomFilter bloom;
        int bloomPos = 0;
    };

    std::map<PrivacySegment*, Info> m_segmentInfos;
};

#endif
