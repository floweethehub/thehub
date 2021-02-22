/*
 * This file is part of the Flowee project
 * Copyright (C) 2019-2021 Tom Zander <tom@flowee.org>
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

#include <APIProtocol.h>
#include <NetworkManager.h>
#include <WorkerThreads.h>
#include <streaming/MessageBuilder.h>

#include <boost/thread/tss.hpp>

#include <deque>
#include <map>
#include <mutex>

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

    Streaming::BufferPool &pool(int reserve);

    WorkerThreads workers;
    NetworkManager network;

    boost::thread_specific_ptr<Streaming::BufferPool> pools;

    // TODO make thread-safe
    struct Connection {
        NetworkConnection con;
        std::set<Service> services;
    };
    std::deque<Connection> connections;

    std::mutex lock;
    std::map<int, Search*> searchers;
    int nextRequestId;

    std::string configFile;

    SearchEngine *q;

    // policies
    SearchPolicy *txPolicy = nullptr;

    // Once we connected once to service we store it here.
    // Higher layers use this info to understand if the reason we can't find a
    // service is temporarily, or a likely setup issue if we never connected
    // said service.
    std::set<Service> m_seenServices;

    // avoid unexpected usage.
    SearchEnginePrivate(const SearchEnginePrivate&) = delete;
    SearchEnginePrivate&operator=(const SearchEnginePrivate&) = delete;
};

class SearchPolicy // move all this to the private instead and make the searcher link to the private instead of having a policy
{
public:
    explicit SearchPolicy(SearchEnginePrivate *parent) : m_owner(parent) {}

    void parseMessageFromHub(Search *request, const Message &message);
    void parseMessageFromIndexer(Search *request, const Message &message);
    void processRequests(Search *request);

    void searchFinished(Search *request);
    /**
     * @brief updateTxRefs uses the txRefs to update the back-ref in the answer list of the request.
     *
     * The client can insert txRefs at the same time they create a job. For instance to fetch a transaction
     * that matches an input of my current transaction I can start a new FetchTx:
     *
     * @code
     *    auto job = Blockchain::Job();
     *    job.type = Blockchain::FetchTx;
     *    job.data = prevTxId;
     *    txRefs.insert(std::make_pair(jobs.size(), txRefKey(requestingAnswerIndex, TxRef::Input, curInputIndex)));
     *    jobs.push_back(job);
     * @endcode
     *
     * The Blockchain baseclass will then call the updateTxRefs() when data has been downloaded.
     * The updateTxRefs method will assume that the answer arrays most recently pushed Transaction is the result
     * of such a job, and ensure that the Transaction at "requestingAnswerIndex" gets a reference to this new
     * transaction.
     */
    void updateTxRefs(Search *request, int jobId);

protected:
    void sendMessage(Search *request, Message message, Service service);
    void updateJob(int jobIndex, Search *request, const Streaming::ConstBuffer &data, int intData1, int intData2);

private:
    SearchEnginePrivate *m_owner;
};

}
#endif
