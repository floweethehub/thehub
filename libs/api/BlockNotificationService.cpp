/*
 * This file is part of the Flowee project
 * Copyright (C) 2018-2021 Tom Zander <tom@flowee.org>
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
#include "BlockNotificationService.h"
#include <APIProtocol.h>

#include <Logger.h>
#include <Message.h>
#include <chain.h>
#include <primitives/FastBlock.h>

#include <streaming/MessageBuilder.h>

BlockNotificationService::BlockNotificationService()
    : NetworkService(Api::BlockNotificationService)
{
    ValidationNotifier().addListener(this);
}

BlockNotificationService::~BlockNotificationService()
{
    ValidationNotifier().removeListener(this);
}

void BlockNotificationService::syncAllTransactionsInBlock(const FastBlock &, CBlockIndex *index)
{
    const auto list = remotes<RemoteWithBool>(&NetworkService::filterRemoteWithBool);
    if (list.empty())
        return;
    m_pool.reserve(45);
    Streaming::MessageBuilder builder(m_pool);
    builder.add(Api::BlockNotification::BlockHash, index->GetBlockHash());
    builder.add(Api::BlockNotification::BlockHeight, index->nHeight);
    Message message(builder.message(Api::BlockNotificationService, Api::BlockNotification::NewBlockOnChain));

    for (auto &subinfo : list) {
        subinfo->connection.send(message);
    }
}

void BlockNotificationService::chainReorged(CBlockIndex *oldTip, const std::vector<FastBlock> &revertedBlocks)
{
    const auto list = remotes<RemoteWithBool>(&NetworkService::filterRemoteWithBool);
    if (list.empty())
        return;

    /*
     * since the service already sends out which block is the new one in a separate message,
     * all we will do in this one is notify them which blocks have been removed.
     */
    m_pool.reserve(revertedBlocks.size() * 42);
    CBlockIndex *index = oldTip;
    Streaming::MessageBuilder builder(m_pool);
    for (size_t i = 0; i < revertedBlocks.size(); ++i) {
        assert(index);
        builder.add(Api::BlockNotification::BlockHash, index->GetBlockHash());
        builder.add(Api::BlockNotification::BlockHeight, index->nHeight);
        index = index->pprev;
    }
    Message message(builder.message(Api::BlockNotificationService, Api::BlockNotification::BlocksRemoved));

    for (auto &subinfo : list) {
        subinfo->connection.send(message);
    }
}

void BlockNotificationService::onIncomingMessage(Remote *remote_, const Message &message, const EndPoint &ep)
{
    assert(dynamic_cast<RemoteWithBool*>(remote_));
    RemoteWithBool *remote = static_cast<RemoteWithBool*>(remote_);
    if (message.messageId() == Api::BlockNotification::Subscribe) {
        logInfo(Log::BlockNotifactionService) << "Remote" << ep.connectionId << "Wants to hear about blocks";
        remote->enabled = true;
    }
    else if (message.messageId() == Api::BlockNotification::Unsubscribe)
        remote->enabled = false;
}
