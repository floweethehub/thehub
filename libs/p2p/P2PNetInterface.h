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
#ifndef P2PNETINTERFACE_H
#define P2PNETINTERFACE_H

#include "PeerAddressDB.h"

class P2PNetInterface
{
public:
    P2PNetInterface() = default;
    virtual ~P2PNetInterface();

    virtual void newPeer(int peerId, const std::string &userAgent, int startHeight, PeerAddress address) {}
    virtual void lostPeer(int peerId) {}
    virtual void punishMentChanged(int peerId) {}

    virtual void blockchainHeightChanged(int newHeight) {}
};

#endif
