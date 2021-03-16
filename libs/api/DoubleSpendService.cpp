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
#include "DoubleSpendService.h"

#include <streaming/MessageBuilder.h>
#include <streaming/streams.h>
#include <Message.h>
#include <version.h>
#include <APIProtocol.h>

#include <DoubleSpendProof.h>

DoubleSpendService::DoubleSpendService()
    : NetworkService(Api::DoubleSpendNotificationService)
{
    ValidationNotifier().addListener(this);
}

DoubleSpendService::~DoubleSpendService()
{
    ValidationNotifier().removeListener(this);
}

void DoubleSpendService::onIncomingMessage(Remote *remote_, const Message &message, const EndPoint &ep)
{
    assert(dynamic_cast<RemoteWithBool*>(remote_));
    RemoteWithBool *remote = static_cast<RemoteWithBool*>(remote_);
    if (message.messageId() == Api::DSP::Subscribe) {
        logInfo(Log::BlockNotifactionService) << "Remote" << ep.connectionId << "Wants to hear about blocks";
        remote->enabled = true;
    }
    else if (message.messageId() == Api::DSP::Unsubscribe) {
        remote->enabled = false;
    }
}

void DoubleSpendService::doubleSpendFound(const Tx &first, const Tx &duplicate)
{
    const auto list = remotes<RemoteWithBool>(&NetworkService::filterRemoteWithBool);
    if (list.empty())
        return;

    m_pool.reserve(40 + duplicate.size());
    Streaming::MessageBuilder builder(m_pool);
    builder.add(Api::DSP::TxId, first.createHash());
    builder.add(Api::DSP::Transaction, duplicate.data());
    Message message(builder.message(Api::DoubleSpendNotificationService, Api::DSP::NewDoubleSpend));

    for (auto &remote : list) {
        remote->connection.send(message);
    }
}

void DoubleSpendService::doubleSpendFound(const Tx &txInMempool, const DoubleSpendProof &proof)
{
    const auto list = remotes<RemoteWithBool>(&NetworkService::filterRemoteWithBool);
    if (list.empty())
        return;

    CDataStream serializedProof(SER_NETWORK, PROTOCOL_VERSION);
    serializedProof << proof;
    m_pool.reserve(40 + serializedProof.size());
    Streaming::MessageBuilder builder(m_pool);
    builder.add(Api::DSP::TxId, txInMempool.createHash());
    builder.addByteArray(Api::DSP::DoubleSpendProofData, serializedProof.const_data(), serializedProof.size());
    Message message(builder.message(Api::DoubleSpendNotificationService, Api::DSP::NewDoubleSpend));

    for (auto &remote : list) {
        remote->connection.send(message);
    }
}
