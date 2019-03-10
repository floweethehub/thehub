/*
 * This file is part of the Flowee project
 * Copyright (C) 2018-2019 Tom Zander <tomz@freedommail.ch>
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
#ifndef BLOCKNOTIFICATIONSERVICE_H
#define BLOCKNOTIFICATIONSERVICE_H

#include <validationinterface.h>
#include <NetworkSubscriptionService.h>

#include <streaming/BufferPool.h>

class BlockNotificationService : public ValidationInterface, public NetworkSubscriptionService
{
public:
    BlockNotificationService();
    ~BlockNotificationService();

    // the hub pushed a transaction into its mempool
    void SyncAllTransactionsInBlock(const FastBlock &block, CBlockIndex *index) override;

protected:

    class RemoteSubscriptionInfo : public Remote {
    public:
        bool m_wantsNewBlockHashes = false;
    };
    // NetworkSubscriptionService interface
    Remote *createRemote() override {
        return new RemoteSubscriptionInfo();
    }

private:
    void handle(Remote *con, const Message &message, const EndPoint &ep) override;

    Streaming::BufferPool m_pool;
};

#endif
