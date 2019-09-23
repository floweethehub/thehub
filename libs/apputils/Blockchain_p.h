/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
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
#ifndef BLOCKCHAIN_P_H
#define BLOCKCHAIN_P_H

#include "Blockchain.h"

#include <QString>
#include <QMutex>
#include <QThreadStorage>

#include <APIProtocol.h>
#include <NetworkManager.h>
#include <WorkerThreads.h>
#include <streaming/MessageBuilder.h>

#include <deque>
#include <map>

namespace Blockchain {

enum HeaderTags
{
    SearchRequestId = Api::RequestId + 1,
    JobRequestId
};

class SearchEnginePrivate {
public:
    SearchEnginePrivate(SearchEngine *q);

    // parse config file and connect to services.
    void findServices();

    void hubConnected(const EndPoint &ep);
    void hubDisconnected(const EndPoint &ep);
    void hubSentMessage(const Message &message);

    void indexerConnected(const EndPoint &ep);
    void indexerDisconnected(const EndPoint &ep);
    void indexerSentMessage(const Message &message);


    void sendMessage(const Message &message, Service service);
    void searchFinished(Search *searcher);

    Streaming::BufferPool *pool();

    WorkerThreads workers;
    NetworkManager network;

    QThreadStorage<Streaming::BufferPool*> pools;

    // TODO make thread-safe
    struct Connection {
        NetworkConnection con;
        std::set<Service> services;
    };
    std::deque<Connection> connections;

    QMutex lock;
    std::map<int, Search*> searchers;
    int nextRequestId;

    QString configFile;

    SearchEngine *q;

    // policies
    SearchPolicy *txPolicy = nullptr;
};

class SearchPolicy // move all this to the private instead and make the searcher link to the private instead of having a policy
{
public:
    explicit SearchPolicy(SearchEnginePrivate *parent) : m_owner(parent) {}

    void parseMessageFromHub(Search *request, const Message &message);
    void parseMessageFromIndexer(Search *request, const Message &message);
    void processRequests(Search *request);

    void searchFinished(Search *request);

protected:
    void sendMessage(Search *request, Message message, Service service);
    void updateJob(int jobIndex, Search *request, const Streaming::ConstBuffer &data, int intData1, int intData2);

private:
    SearchEnginePrivate *m_owner;
};

}
#endif
