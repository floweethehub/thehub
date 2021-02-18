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
#include "BlockNotificationService.h"
#include <APIProtocol.h>

#include <Logger.h>
#include <Message.h>
#include <chain.h>

#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

// #include "Application.h"

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
    const auto remotes_ = remotes();
    if (remotes_.empty())
        return;
    m_pool.reserve(45);
    Streaming::MessageBuilder builder(m_pool);
    builder.add(Api::BlockNotification::BlockHash, index->GetBlockHash());
    builder.add(Api::BlockNotification::BlockHeight, index->nHeight);
    Message message(builder.message(Api::BlockNotificationService, Api::BlockNotification::NewBlockOnChain));

    for (auto remote : remotes_) {
        RemoteSubscriptionInfo *subinfo = static_cast<RemoteSubscriptionInfo*>(remote);
        if (subinfo->m_wantsNewBlockHashes)
            subinfo->connection.send(message);
    }
}

void BlockNotificationService::onIncomingMessage(Remote *remote_, const Message &message, const EndPoint &ep)
{
    assert(dynamic_cast<RemoteSubscriptionInfo*>(remote_));
    RemoteSubscriptionInfo *remote = static_cast<RemoteSubscriptionInfo*>(remote_);
    if (message.messageId() == Api::BlockNotification::Subscribe) {
        logInfo(Log::BlockNotifactionService) << "Remote" << ep.connectionId << "Wants to hear about blocks";
        remote->m_wantsNewBlockHashes = true;
    }

    else if (message.messageId() == Api::BlockNotification::Unsubscribe)
        remote->m_wantsNewBlockHashes = false;
}
