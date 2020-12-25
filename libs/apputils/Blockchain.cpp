/*
 * This file is part of the Flowee project
 * Copyright (C) 2019-2020 Tom Zander <tomz@freedommail.ch>
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
#include "Blockchain_p.h"

#include <utilstrencodings.h>
#include <streaming/BufferPool.h>
#include <streaming/MessageParser.h>
#include <primitives/FastTransaction.h>

#include <boost/filesystem.hpp>
#include <boost/program_options/detail/config_file.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/algorithm/string.hpp>

namespace {

Blockchain::Transaction fillTx(Streaming::MessageParser &parser, const Blockchain::Job &job, int jobId)
{
    Blockchain::Transaction tx;
    tx.jobId = jobId;
    if (job.type == Blockchain::FetchTx) {
        tx.blockHeight = job.intData;
        tx.offsetInBlock = job.intData2;
    }
    if (job.type == Blockchain::FetchTx || job.type == Blockchain::FindTxInMempool)
        tx.txid = job.data;

    // The vast majority of the LiveTransaction and the Blockchain tags are identical
    // in this method we use the lowest common denominator when available.
    // This is why the tags are typically compared to the Api::Blockchain::Tags enum values

    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::TxId)
            tx.txid = parser.bytesDataBuffer();
        else if (parser.tag() == Api::BlockHeight)
            tx.blockHeight = parser.intData();
        else if (parser.tag() == Api::OffsetInBlock)
            tx.offsetInBlock = parser.intData();
        else if (parser.tag() == Api::GenericByteData)
            tx.fullTxData = parser.bytesDataBuffer();
        else if (parser.tag() == Api::BlockChain::Tx_IN_TxId) {
            tx.inputs.resize(tx.inputs.size() + 1);
            tx.inputs.back().prevTxId = parser.bytesDataBuffer();
        }
        else if (parser.tag() == Api::BlockChain::Tx_InputScript) {
            if (tx.inputs.empty()) // needed because for coinbase there is no Tx_IN_TxId
                tx.inputs.resize(1);
            tx.inputs.back().inputScript = parser.bytesDataBuffer();
        }
        else if (parser.tag() == Api::BlockChain::Tx_IN_OutIndex) {
            assert (tx.inputs.size() != 0);
            tx.inputs.back().outIndex = parser.intData();
        }
        else if (parser.tag() == Api::BlockChain::Tx_Out_Index) {
            tx.outputs.resize(tx.outputs.size() + 1);
            assert(parser.intData() < 0xFFFF);
            tx.outputs.back().index = static_cast<short>(parser.intData());
        }
        else if (parser.tag() == Api::BlockChain::Tx_Out_Amount) {
            assert (tx.outputs.size() != 0);
            tx.outputs.back().amount = parser.longData();
        }
        else if (parser.tag() == Api::BlockChain::Tx_OutputScript) {
            assert (tx.outputs.size() != 0);
            tx.outputs.back().outScript = parser.bytesDataBuffer();
            tx.outputs.back().type = Blockchain::Output::FullScript;
        }
        else if (parser.tag() == Api::BlockChain::Tx_Out_Address) {
            assert (tx.outputs.size() != 0);
            tx.outputs.back().outScript = parser.bytesDataBuffer();
            tx.outputs.back().type = Blockchain::Output::OnlyAddress;
        }
        else if (parser.tag() == Api::BlockChain::GenericByteData
                 || (job.type == Blockchain::FindTxInMempool
                     && parser.tag() == Api::LiveTransactions::Transaction)) {
            tx.fullTxData = parser.bytesDataBuffer();
        }
        else if (job.type == Blockchain::FindTxInMempool
                 && parser.tag() == Api::LiveTransactions::FirstSeenTime) {
            // This assumes that the blockHeight is not set since otherwise we won't
            // be able to tell the difference.
            assert(tx.blockHeight == -1);
            tx.firstSeenTime = parser.longData(); // use long to be safe for 2038 overflow
        }
        else if (job.type == Blockchain::FindAddressInMempool
                 && parser.tag() == Api::LiveTransactions::MatchingOutIndex) {
            tx.outIndex = parser.intData();
        }
        else if (parser.tag() == Api::Separator) {
            break;
        }
    }

    if (tx.txid.isEmpty() && !tx.fullTxData.isEmpty()) {
        // then lets fill it.
        Streaming::BufferPool pool(32);
        Tx fullTx(tx.fullTxData);
        auto hash = fullTx.createHash();
        memcpy(pool.begin(), hash.begin(), 32);
        tx.txid = pool.commit(32);
    }
    return tx;
}

void addIncludeRequests(Streaming::MessageBuilder &builder, uint32_t transactionFilters)
{
    if (transactionFilters & Blockchain::IncludeInputs)
        builder.add(Api::BlockChain::Include_Inputs, true);
    builder.add(Api::BlockChain::Include_TxId, (transactionFilters & Blockchain::IncludeTxId) != 0);
    builder.add(Api::BlockChain::FullTransactionData, (transactionFilters & Blockchain::IncludeFullTransactionData) != 0);
    if (transactionFilters & Blockchain::IncludeOutputs)
        builder.add(Api::BlockChain::Include_Outputs, true);
    if (transactionFilters & Blockchain::IncludeOutputAmounts)
        builder.add(Api::BlockChain::Include_OutputAmounts, true);
    if (transactionFilters & Blockchain::IncludeOutputScripts)
        builder.add(Api::BlockChain::Include_OutputScripts, true);
    if (transactionFilters & Blockchain::IncludeOutputAddresses)
        builder.add(Api::BlockChain::Include_OutputAddresses, true);
}

}


Blockchain::ServiceUnavailableException::ServiceUnavailableException(const char *error, Blockchain::Service service)
    : std::runtime_error(error),
      m_service(service)
{
}


Blockchain::Search::~Search()
{
    if (policy)
        policy->searchFinished(this);
}

uint64_t Blockchain::Search::txRefKey(int origTxIndex, Blockchain::Search::TxRef ref, int index)
{
    // pack those 3 into one long.
    assert(origTxIndex >= 0);
    assert(index >= 0);
    assert(index < 1000000); // should fit in 24 bits.
    uint64_t answer = origTxIndex;
    answer <<= 32;
    if (ref == TxRef::Input)
        answer += 0x1000000;
    answer += index;
    return answer;
}

Blockchain::SearchEngine::SearchEngine()
    : d(new SearchEnginePrivate(this))
{
}

Blockchain::SearchEngine::~SearchEngine()
{
    delete d;
}

void Blockchain::SearchEngine::start(Blockchain::Search *request)
{
    request->policy = d->txPolicy;
    {
        std::lock_guard<std::mutex> lock(d->lock);
        request->requestId = d->nextRequestId++;
        d->searchers.insert(std::make_pair(request->requestId, request));
    }
    request->policy->processRequests(request);
}

void Blockchain::SearchEngine::addIndexer(const EndPoint &ep)
{
    auto connection = d->network.connection(ep);
    if (!connection.isValid())
        throw std::runtime_error("Invalid Endpoint, can't create Indexer connection");
    connection.setOnConnected(std::bind(&SearchEnginePrivate::indexerConnected, d, std::placeholders::_1));
    connection.setOnDisconnected(std::bind(&SearchEnginePrivate::indexerDisconnected, d, std::placeholders::_1));
    connection.setOnIncomingMessage(std::bind(&SearchEnginePrivate::indexerSentMessage, d, std::placeholders::_1));
    d->connections.resize(d->connections.size() + 1);
    SearchEnginePrivate::Connection &c = d->connections.back();
    c.con = std::move(connection);
    c.con.setMessageQueueSizes(60000, 1);
    c.con.connect();
}

void Blockchain::SearchEngine::addHub(const EndPoint &ep)
{
    auto connection = d->network.connection(ep);
    if (!connection.isValid())
        throw std::runtime_error("Invalid Endpoint, can't create Hub connection");
    connection.setOnConnected(std::bind(&SearchEnginePrivate::hubConnected, d, std::placeholders::_1));
    connection.setOnDisconnected(std::bind(&SearchEnginePrivate::hubDisconnected, d, std::placeholders::_1));
    connection.setOnIncomingMessage(std::bind(&SearchEnginePrivate::hubSentMessage, d, std::placeholders::_1));
    d->connections.resize(d->connections.size() + 1);
    SearchEnginePrivate::Connection &c = d->connections.back();
    c.con = std::move(connection);
    c.con.setMessageQueueSizes(60000, 1);
    c.con.connect();
}

void Blockchain::SearchEngine::setConfigFile(const std::string &configFile)
{
    d->configFile = configFile;
    reparseConfig();
}

void Blockchain::SearchEngine::parseConfig(const std::string &confFile)
{
    // intentionally left empty
}

void Blockchain::SearchEngine::reparseConfig()
{
    d->findServices();
    parseConfig(d->configFile);
}

Streaming::BufferPool &Blockchain::SearchEngine::poolForThread()
{
    return *d->pool();
}

void Blockchain::SearchEngine::sendMessage(const Message &message, Blockchain::Service service)
{
    d->sendMessage(message, service);
}

bool Blockchain::SearchEngine::isHubConnected() const
{
    for (auto iter = d->connections.begin(); iter != d->connections.end(); ++iter) {
        if (iter->services.find(TheHub) != iter->services.end())
            return true;
    }
    return false;
}

bool Blockchain::SearchEngine::isIndexerConnected() const
{
    for (auto iter = d->connections.begin(); iter != d->connections.end(); ++iter) {
        if (iter->services.find(IndexerTxIdDb) != iter->services.end())
            return true;
    }
    return false;
}

Blockchain::SearchEnginePrivate::SearchEnginePrivate(SearchEngine *q)
    : network(workers.ioService()),
      nextRequestId(1),
      q(q)
{
    txPolicy = new SearchPolicy(this);
}

void Blockchain::SearchEnginePrivate::findServices()
{
    logInfo(Log::SearchEngine) << "parsing config" << configFile;

    boost::filesystem::ifstream streamConfig(configFile);
    if (!streamConfig.good())
        return; // No conf file is OK

    std::set<std::string> setOptions;
    setOptions.insert("*");

    using namespace boost::program_options;

    EndPoint ep;
    for (detail::config_file_iterator it(streamConfig, setOptions), end; it != end; ++it) {
        if (it->string_key == "services.indexer" && !it->value.empty()) {
            std::vector<std::string> indexers;
            boost::split(indexers, it->value[0], boost::is_any_of(" \t;,"));
            for (auto &indexer : indexers) {
                try {
                    ep.announcePort = 1234;
                    SplitHostPort(indexer, ep.announcePort, ep.hostname);
                    if (!network.connection(ep, NetworkManager::OnlyExisting).isValid())
                        q->addIndexer(ep);
                } catch (const std::exception &e) {
                    logCritical(Log::SearchEngine) << "Connecting to" << indexer << ep.announcePort << "failed with:" << e;
                }
            }
        }
        else if (it->string_key == "services.hub" && !it->value.empty()) {
            std::vector<std::string> hubs;
            boost::split(hubs, it->value[0], boost::is_any_of(" \t;,"));
            for (auto &hub : hubs) {
                try {
                    ep.announcePort = 1235;
                    SplitHostPort(hub, ep.announcePort, ep.hostname);
                    if (!network.connection(ep, NetworkManager::OnlyExisting).isValid())
                        q->addHub(ep);
                } catch (const std::exception &e) {
                    logCritical(Log::SearchEngine) << "Connecting to" << hub << ep.announcePort << "failed with:" << e;
                }
            }
        }
    }
}

void Blockchain::SearchEnginePrivate::hubConnected(const EndPoint &ep)
{
    logDebug(Log::SearchEngine);
    auto con = network.connection(ep);
    con.send(Message(Api::APIService, Api::Meta::Version));
}

void Blockchain::SearchEnginePrivate::hubDisconnected(const EndPoint &ep)
{
    // TODO unset flag in connections
    logDebug(Log::SearchEngine);
    q->hubDisconnected();
}

void Blockchain::SearchEnginePrivate::hubSentMessage(const Message &message)
{
    const int id = message.headerInt(SearchRequestId);
    if (id > 0) {
        logDebug(Log::SearchEngine) << "Received hub message for search:" << id;
        std::lock_guard<std::mutex> lock_(lock);
        auto searcher = searchers.find(id);
        if (searcher != searchers.end()) {
            assert(searcher->second->policy);
            try {
                searcher->second->policy->parseMessageFromHub(searcher->second, message);
            } catch (const Blockchain::ServiceUnavailableException &e) {
                logWarning() << "Service unavailable" << e;
                searcher->second->aborted(e);
            }
        }
        else logDebug() << "No searcher matching the job";
        return;
    }
    if (message.serviceId() == Api::APIService && message.messageId() == Api::Meta::VersionReply) {
        Streaming::MessageParser parser(message);
        std::string hubId;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::GenericByteData) {
                hubId = parser.stringData();
                logCritical(Log::SearchEngine) << "  Upstream hub connected" << hubId;
                if (parser.stringData().compare("Flowee:1 (2019-9.1)") < 0) {
                    logFatal() << "  Hub is too old, not using";
                    return;
                }
                break;
            }
        }
        // find connection in connections and set flag that this is a known hub
        for (auto iter = connections.begin(); iter != connections.end(); ++iter) {
            if (iter->con.connectionId() == message.remote) {
                iter->services.insert(TheHub);
                break;
            }
        }
        // then as last thing, let our subclasses know;
        q->initializeHubConnection(network.connection(network.endPoint(message.remote)), hubId);
        return;
    }
    q->hubSentMessage(message);
}

void Blockchain::SearchEnginePrivate::indexerConnected(const EndPoint &ep)
{
    logDebug(Log::SearchEngine);
    auto con = network.connection(ep);
    con.send(Message(Api::IndexerService, Api::Indexer::GetAvailableIndexers));
    con.send(Message(Api::IndexerService, Api::Indexer::Version));
}

void Blockchain::SearchEnginePrivate::indexerDisconnected(const EndPoint &)
{
    // TODO unset flag in connections
    logDebug(Log::SearchEngine);
    q->indexerDisconnected();
}

void Blockchain::SearchEnginePrivate::indexerSentMessage(const Message &message)
{
    logDebug(Log::SearchEngine);
    const int id = message.headerInt(SearchRequestId);
    if (id > 0) {
        std::lock_guard<std::mutex> lock_(lock);
        auto searcher = searchers.find(id);
        if (searcher != searchers.end()) {
            try {
                searcher->second->policy->parseMessageFromIndexer(searcher->second, message);
            } catch (const Blockchain::ServiceUnavailableException &e) {
                logWarning() << "Service unavailable" << e;
                searcher->second->aborted(e);
            }
        }
        return;
    }

    // parse message on which indexers the indexer has.
    if (message.serviceId() == Api::IndexerService) {
        if (message.messageId() == Api::Indexer::GetAvailableIndexersReply) {
            bool hasTxId = false, hasSpent = false, hasAddress = false;
            Streaming::MessageParser p(message);
            while (p.next() == Streaming::FoundTag) {
                if (p.tag() == Api::Indexer::AddressIndexer) {
                    hasAddress = true;
                    logInfo(Log::SearchEngine) << "Indexer 'address' available:" << p.boolData();
                } else if (p.tag() == Api::Indexer::TxIdIndexer) {
                    hasTxId = true;
                    logInfo(Log::SearchEngine) << "Indexer 'TxID' available:" << p.boolData();
                } else if (p.tag() == Api::Indexer::SpentOutputIndexer){
                    hasSpent = true;
                    logInfo(Log::SearchEngine) << "Indexer 'Spent' available:" << p.boolData();
                }
            }

            for (auto iter = connections.begin(); iter != connections.end(); ++iter) {
                if (iter->con.connectionId() == message.remote) {
                    if (hasAddress)
                        iter->services.insert(IndexerAddressDb);
                    if (hasTxId)
                        iter->services.insert(IndexerTxIdDb);
                    if (hasSpent)
                        iter->services.insert(IndexerSpentDb);
                    break;
                }
            }
            std::set<Service> services;
            if (hasAddress)
                services.insert(IndexerAddressDb);
            if (hasTxId)
                services.insert(IndexerTxIdDb);
            if (hasSpent)
                services.insert(IndexerSpentDb);

            q->initializeIndexerConnection(network.connection(network.endPoint(message.remote)), services);
            return;
        }
        if (message.messageId() == Api::Indexer::VersionReply) {
            Streaming::MessageParser parser(message);
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::GenericByteData) {
                    logCritical(Log::SearchEngine) << "  Upstream Indexer connected"
                                                   << parser.stringData();
                    return;
                }
            }
        }
    }
    q->indexerSentMessage(message);
}

void Blockchain::SearchEnginePrivate::sendMessage(const Message &message, Blockchain::Service service)
{
    auto iter = connections.begin();
    while (iter != connections.end()) {
        if (iter->services.find(service) != iter->services.end()) {
            iter->con.send(message);
            return;
        }
        ++iter;
    }
    throw ServiceUnavailableException("Backing service not connected", service);
}

void Blockchain::SearchEnginePrivate::searchFinished(Blockchain::Search *searcher)
{
    std::lock_guard<std::mutex> lock_(lock);
    auto iter = searchers.find(searcher->requestId);
    if (iter != searchers.end())
        searchers.erase(iter);
}

Streaming::BufferPool *Blockchain::SearchEnginePrivate::pool()
{
    if (!pools.get())
        pools.reset(new Streaming::BufferPool(1E6));
    return pools.get();
}

void Blockchain::SearchPolicy::parseMessageFromHub(Search *request, const Message &message)
{
    const int jobId = message.headerInt(JobRequestId);
    logDebug(Log::SearchEngine) << "  " << jobId;
    Streaming::MessageParser parser(message);
    Job job;
    { // jobsLock scope
    std::lock_guard<std::mutex> lock(request->jobsLock);
    if (jobId < 0 || static_cast<int>(request->jobs.size()) <= jobId) {
        logDebug(Log::SearchEngine) << "Hub message refers to non existing job Id";
        return;
    }
    Job &job_ref = request->jobs[jobId];
    job_ref.finished = true;
    job = job_ref;
    } // jobsLock scope

    if (message.serviceId() == Api::BlockChainService) {
        if (message.messageId() == Api::BlockChain::GetTransactionReply) {
            request->answer.push_back(fillTx(parser, job, jobId));
            const Transaction &tx = request->answer.back();
            updateTxRefs(request, jobId);
            request->transactionAdded(tx, request->answer.size() - 1);
        }
        else if (message.messageId() == Api::BlockChain::GetBlockHeaderReply) {
            BlockHeader header;
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::BlockChain::BlockHash)
                    header.hash = parser.bytesDataBuffer();
                else if (parser.tag() == Api::BlockChain::Confirmations)
                    header.confirmations = parser.intData();
                else if (parser.tag() == Api::BlockChain::BlockHeight)
                    header.height = parser.intData();
                else if (parser.tag() == Api::BlockChain::Version)
                    header.version = static_cast<uint32_t>(parser.longData());
                else if (parser.tag() == Api::BlockChain::MerkleRoot)
                    header.merkleRoot = parser.bytesDataBuffer();
                else if (parser.tag() == Api::BlockChain::Time)
                    header.time = static_cast<uint32_t>(parser.longData());
                else if (parser.tag() == Api::BlockChain::MedianTime)
                    header.medianTime = static_cast<uint32_t>(parser.longData());
                else if (parser.tag() == Api::BlockChain::Nonce)
                    header.nonce = static_cast<uint32_t>(parser.longData());
                else if (parser.tag() == Api::BlockChain::Bits)
                    header.bits = static_cast<uint32_t>(parser.longData());
                else if (parser.tag() == Api::BlockChain::Difficulty)
                    header.difficulty = parser.doubleData();
            }
            if (header.height > 0)
                request->blockHeaders.insert(std::make_pair(header.height, header));
        }
        else if (message.messageId() == Api::BlockChain::GetBlockReply) {
            while (true) {
                bool more;
                parser.peekNext(&more);
                if (!more)
                    break;
                request->answer.push_back(fillTx(parser, job, jobId));
                request->transactionAdded(request->answer.back(), request->answer.size() - 1);
            }
        }
        else {
            logDebug(Log::SearchEngine) << "Unknown message from Hub" << message.serviceId() << message.messageId();
        }
    }

    if (message.serviceId() == Api::LiveTransactionService) {
        if (message.messageId() == Api::LiveTransactions::IsUnspentReply
                || message.messageId() == Api::LiveTransactions::GetUnspentOutputReply) {
            int blockHeight = job.intData;
            int offsetInBlock = job.intData2;
            int outIndex = job.intData3;
            int64_t amount = -1;
            Streaming::ConstBuffer outputScript;
            bool unspent = false;
            while (parser.next() == Streaming::FoundTag) {
                switch (parser.tag()) {
                case Api::LiveTransactions::BlockHeight:
                    blockHeight = parser.intData();
                    break;
                case Api::LiveTransactions::OffsetInBlock:
                    offsetInBlock = parser.intData();
                    break;
                case Api::LiveTransactions::UnspentState:
                    unspent = parser.boolData();
                    break;
                case Api::LiveTransactions::OutIndex:
                    outIndex = parser.intData();
                    break;
                case Api::LiveTransactions::Amount:
                    amount = int64_t(parser.longData());
                    break;
                case Api::LiveTransactions::OutputScript:
                    outputScript = parser.bytesDataBuffer();
                    break;
                }
            }
            request->utxoLookup(jobId, blockHeight, offsetInBlock, outIndex, unspent, amount, outputScript);
        }

        if (message.messageId() == Api::LiveTransactions::SendTransactionReply) {
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::GenericByteData) {
                    Transaction tx;
                    tx.txid = parser.bytesDataBuffer();
                    tx.jobId = jobId;
                    request->answer.push_back(tx);
                    break;
                }
            }
        }
        if (message.messageId() == Api::LiveTransactions::SearchMempoolReply) {
            while (true) {
                bool more;
                parser.peekNext(&more);
                if (!more)
                    break;
                request->answer.push_back(fillTx(parser, job, jobId));
                updateTxRefs(request, jobId);
                request->transactionAdded(request->answer.back(), request->answer.size() - 1);
            }
        }
    }
    else if (message.serviceId() == Api::APIService && message.messageId() == Api::Meta::CommandFailed) {
        Error error;
        while (parser.next() == Streaming::FoundTag) {
            switch (parser.tag()) {
            case Api::Meta::FailedReason: error.error = parser.stringData(); break;
            case Api::Meta::FailedCommandServiceId: error.serviceId = parser.intData(); break;
            case Api::Meta::FailedCommandId: error.messageId = parser.intData(); break;
            default: break;
            }
        }
        request->errors.insert(std::make_pair(jobId, error));
    }
    else if (message.serviceId() != Api::BlockChainService) {
        logDebug(Log::SearchEngine) << "Unknown message from Hub" << message.serviceId() << message.messageId();
        Streaming::MessageParser::debugMessage(message);
    }

    processRequests(request);
}

void Blockchain::SearchPolicy::parseMessageFromIndexer(Search *request, const Message &message)
{
    const int jobId = message.headerInt(JobRequestId);
    logDebug(Log::SearchEngine) << "  " << jobId;
    { // jobslock scope
    std::lock_guard<std::mutex> lock(request->jobsLock);
    if (jobId < 0 || static_cast<int>(request->jobs.size()) <= jobId) {
        logDebug(Log::SearchEngine) << "Indexer message refers to non existing job Id";
        return;
    }
    Job &job = request->jobs[jobId];
    job.finished = true;

    Streaming::MessageParser parser(message);
    if (message.messageId() == Api::Indexer::FindTransactionReply
            || message.messageId() == Api::Indexer::FindSpentOutputReply) {
        int height = 0;
        int offsetInBlock = 0;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockHeight)
                height = parser.intData();
            else if (parser.tag() == Api::OffsetInBlock)
                offsetInBlock = parser.intData();
        }
        if (height != -1) { // only update jobs when we actually found the thing we were looking for.
            updateJob(job.nextJobId, request, job.data, height, offsetInBlock);
            updateJob(job.nextJobId2, request, job.data, height, offsetInBlock);
        }
        if (message.messageId() == Api::Indexer::FindTransactionReply)
            request->txIdResolved(jobId, height, offsetInBlock);
        else
            request->spentOutputResolved(jobId, height, offsetInBlock);
    }
    else if (message.messageId() == Api::Indexer::FindAddressReply) {
        int blockHeight = -1, offsetInBlock = 0;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::Indexer::BlockHeight)
                blockHeight = parser.intData();
            else if (parser.tag() == Api::Indexer::OffsetInBlock)
                offsetInBlock = parser.intData();
            else if (parser.tag() == Api::Indexer::OutIndex)
                request->addressUsedInOutput(blockHeight, offsetInBlock, parser.intData());
        }
    } else {
        logDebug(Log::SearchEngine) << "Unknown message from Indexer";
    }
    } // jobslock scope

    processRequests(request);
}

void Blockchain::SearchPolicy::processRequests(Blockchain::Search *request)
{
    int jobsInFlight = 0;
    int jobsWaiting = 0;
    Streaming::BufferPool *pool = m_owner->pool();
    { // jobsLock scope
    std::lock_guard<std::mutex> lock(request->jobsLock);
    for (size_t i = 0; i < request->jobs.size(); ++i) {
        Job &job = request->jobs.at(i);
        if (job.finished)
            continue;
        if (job.started) {
            jobsInFlight++;
            continue;
        }
        try {
            switch (job.type) {
            case Blockchain::Unset:
                throw std::runtime_error("Invalid job definition");
            case Blockchain::FetchUTXOUnspent:
            case Blockchain::FetchUTXODetails: {
                if (job.data.size() != 32 && (job.intData <= 0 || job.intData2 <= 0))
                    throw std::runtime_error("Invalid job definition");

                pool->reserve(60);
                Streaming::MessageBuilder builder(*pool, Streaming::HeaderAndBody);
                builder.add(Network::ServiceId, Api::LiveTransactionService);
                builder.add(Network::MessageId, (job.type == Blockchain::FetchUTXODetails)
                            ? Api::LiveTransactions::GetUnspentOutput : Api::LiveTransactions::IsUnspent);
                builder.add(SearchRequestId, request->requestId);
                builder.add(JobRequestId, i);
                builder.add(Network::HeaderEnd, true);
                // now decide if I send blockheight/offset or txid
                if (job.data.size() == 32) {
                    builder.add(Api::TxId, job.data);
                    builder.add(Api::LiveTransactions::OutIndex, job.intData);
                } else {
                    builder.add(Api::BlockHeight, job.intData);
                    builder.add(Api::OffsetInBlock, job.intData2);
                    builder.add(Api::LiveTransactions::OutIndex, job.intData3);
                }
                job.started = true;
                sendMessage(request, builder.message(), TheHub);
                break;
            }
            case Blockchain::LookupTxById: {
                if (job.data.size() != 32)
                    throw std::runtime_error("Invalid job definition");
                logDebug(Log::SearchEngine) << "starting lookup (txid)" << i;
                pool->reserve(50);
                Streaming::MessageBuilder builder(*pool, Streaming::HeaderAndBody);
                builder.add(Network::ServiceId, Api::IndexerService);
                builder.add(Network::MessageId, Api::Indexer::FindTransaction);
                builder.add(SearchRequestId, request->requestId);
                builder.add(JobRequestId, i);
                builder.add(Network::HeaderEnd, true);
                builder.add(Api::Indexer::TxId, job.data);
                job.started = true;
                sendMessage(request, builder.message(), IndexerTxIdDb);
                break;
            }
            case Blockchain::LookupByAddress: {
                if (job.data.size() != 32) // expect a sha256 hash of the outputscript here
                    throw std::runtime_error("Invalid job definition");
                logDebug(Log::SearchEngine) << "starting lookup (address)" << i;
                pool->reserve(40);
                Streaming::MessageBuilder builder(*pool, Streaming::HeaderAndBody);
                builder.add(Network::ServiceId, Api::IndexerService);
                builder.add(Network::MessageId, Api::Indexer::FindAddress);
                builder.add(SearchRequestId, request->requestId);
                builder.add(JobRequestId, i);
                builder.add(Network::HeaderEnd, true);
                builder.add(Api::Indexer::BitcoinScriptHashed, job.data);
                job.started = true;
                sendMessage(request, builder.message(), IndexerAddressDb);
                break;
            }
            case Blockchain::LookupSpentTx: {
                if (job.data.size() != 32 || job.intData == -1) // expect a sha256 & outIndex here
                    throw std::runtime_error("Invalid job definition");
                logDebug(Log::SearchEngine) << "starting lookup (spentTx)" << i;
                pool->reserve(40);
                Streaming::MessageBuilder builder(*pool, Streaming::HeaderAndBody);
                builder.add(Network::ServiceId, Api::IndexerService);
                builder.add(Network::MessageId, Api::Indexer::FindSpentOutput);
                builder.add(SearchRequestId, request->requestId);
                builder.add(JobRequestId, i);
                builder.add(Network::HeaderEnd, true);
                builder.add(Api::Indexer::TxId, job.data);
                builder.add(Api::Indexer::OutIndex, job.intData);
                job.started = true;
                sendMessage(request, builder.message(), IndexerSpentDb);
                break;
            }
            case Blockchain::FetchTx:
                if (job.intData && job.intData2) {
                    job.started = true;
                    logDebug(Log::SearchEngine) << "starting fetch TX" << i;
                    // simple, we just send the message.
                    pool->reserve(40);
                    Streaming::MessageBuilder builder(*pool, Streaming::HeaderAndBody);
                    builder.add(Network::ServiceId, Api::BlockChainService);
                    builder.add(Network::MessageId, Api::BlockChain::GetTransaction);
                    builder.add(SearchRequestId, request->requestId);
                    builder.add(JobRequestId, i);
                    builder.add(Network::HeaderEnd, true);
                    builder.add(Api::BlockChain::BlockHeight, job.intData);
                    builder.add(Api::BlockChain::Tx_OffsetInBlock, job.intData2);
                    addIncludeRequests(builder, job.transactionFilters);
                    sendMessage(request, builder.message(), TheHub);
                }
                else if (job.data.size() == 32) {
                    logDebug(Log::SearchEngine) << "Creating two new jobs to lookup and then fetch a TX";
                    job.finished = job.started = true;
                    // first need to do a lookupByTxId
                    Job lookupJob;
                    lookupJob.type = LookupTxById;
                    lookupJob.data = job.data;
                    lookupJob.nextJobId = job.nextJobId;
                    lookupJob.nextJobId2 = static_cast<int>(request->jobs.size() + 1);
                    request->jobs.push_back(lookupJob);
                    Job fetchTxJob;
                    fetchTxJob.type = Blockchain::FetchTx;
                    fetchTxJob.transactionFilters = job.transactionFilters;
                    request->jobs.push_back(fetchTxJob);
                }
                else
                    jobsWaiting++; // Waiting for data
                break;
            case Blockchain::FetchBlockHeader: {
                if (job.data.size() != 32 && !job.intData) {  // Waiting for data
                    jobsWaiting++;
                    continue;
                }
                job.started = true;
                logDebug(Log::SearchEngine) << "starting fetching of block header" << i;
                pool->reserve(60);
                Streaming::MessageBuilder builder(*pool, Streaming::HeaderAndBody);
                builder.add(Network::ServiceId, Api::BlockChainService);
                builder.add(Network::MessageId, Api::BlockChain::GetBlockHeader);
                builder.add(SearchRequestId, request->requestId);
                builder.add(JobRequestId, i);
                builder.add(Network::HeaderEnd, true);
                if (job.intData)
                    builder.add(Api::BlockChain::BlockHeight, job.intData);
                else
                    builder.add(Api::BlockChain::BlockHash, job.data);
                sendMessage(request, builder.message(), TheHub);
                break;
            }
            case Blockchain::CustomHubMessage: {
                if (job.data.size() == 0 || job.intData <= 0  || job.intData2 <= 0) {
                    throw std::runtime_error("Invalid job definition");
                }
                job.started = true;
                logDebug(Log::SearchEngine) << "starting custom Hub message" << i << "SID" << job.intData << "MID" << job.intData2;
                auto m = Message(job.data, job.intData, job.intData2);
                m.setHeaderInt(JobRequestId, i);
                // TODO enable this half 2021 or so.
                    // ASync is ignored for types that don't support it, but SendLiveTransaction for instnace really appreciates this.
                    // m.setHeaderInt(Api::ASyncRequest, 1);
                sendMessage(request, m, TheHub);
                break;
            }
            case Blockchain::FetchBlockOfTx: {
                if (job.data.size() != 32 && !job.intData) {  // Waiting for data
                    jobsWaiting++;
                    continue;
                }
                job.started = true;
                logDebug(Log::SearchEngine) << "starting fetching of block" << i;

                pool->reserve(60);
                Streaming::MessageBuilder builder(*pool, Streaming::HeaderAndBody);
                builder.add(Network::ServiceId, Api::BlockChainService);
                builder.add(Network::MessageId, Api::BlockChain::GetBlock);
                builder.add(SearchRequestId, request->requestId);
                builder.add(JobRequestId, i);
                builder.add(Network::HeaderEnd, true);
                if (job.intData)
                    builder.add(Api::BlockChain::BlockHeight, job.intData);
                else
                    builder.add(Api::BlockChain::BlockHash, job.data);
                addIncludeRequests(builder, job.transactionFilters);
                sendMessage(request, builder.message(), TheHub);
                break;
            }
            case FindTxInMempool:
            case FindAddressInMempool:
                if (job.data.size() != 32)
                    throw std::runtime_error("Invalid job definition");

                job.started = true;
                logDebug(Log::SearchEngine) << "starting Find-Tx in mempool" << i;
                Streaming::MessageBuilder builder(*pool, Streaming::HeaderAndBody);
                builder.add(Network::ServiceId, Api::LiveTransactionService);
                builder.add(Network::MessageId, Api::LiveTransactions::SearchMempool);
                builder.add(SearchRequestId, request->requestId);
                builder.add(JobRequestId, i);
                builder.add(Network::HeaderEnd, true);
                if (job.type == FindTxInMempool)
                    builder.add(Api::LiveTransactions::TxId, job.data);
                else
                    builder.add(Api::LiveTransactions::BitcoinScriptHashed, job.data);
                addIncludeRequests(builder, job.transactionFilters);
                sendMessage(request, builder.message(), TheHub);
                break;
            }
        } catch (const ServiceUnavailableException &e) {
            throw;
        } catch (std::exception &e) {
            logCritical(Log::SearchEngine) << "Job processing failed due to" << e;
            job.started = true;
            job.finished = true;
        }

        if (job.started)
            jobsInFlight++;
    }
    } // jobsLock scope

    if (jobsInFlight == 0)
        request->finished(jobsWaiting);
}

void Blockchain::SearchPolicy::searchFinished(Blockchain::Search *request)
{
    m_owner->searchFinished(request);
}

void Blockchain::SearchPolicy::updateTxRefs(Search *request, int jobId)
{
    auto iter = request->txRefs.find(jobId);
    if (iter != request->txRefs.end()) { // check the txRefs for matches.
        uint64_t v = iter->second;
        uint32_t k = v & 0xFFFFFFFF;
        v = v >> 32;
        int txIndex = v;
        assert(txIndex >= 0); // if this or the next hits, then the inherting class failed to insert a proper row
        assert(request->answer.size() > size_t(txIndex));
        Transaction &backTx = request->answer[txIndex];
        backTx.txRefs.insert(std::make_pair(k, &request->answer.back()));
    }
}

void Blockchain::SearchPolicy::sendMessage(Blockchain::Search *request, Message message, Blockchain::Service service)
{
    if (!message.hasHeader())
        message.setHeaderInt(SearchRequestId, request->requestId);
    m_owner->sendMessage(message, service);
}

void Blockchain::SearchPolicy::updateJob(int jobIndex, Search *request, const Streaming::ConstBuffer &data, int intData1, int intData2)
{
    // assumes the jobsLock is locked by caller
    if (jobIndex == -1)
        return;
    assert(jobIndex >= 0);
    const size_t index = static_cast<size_t>(jobIndex);
    assert(index < request->jobs.size());
    Job &ref = request->jobs[index];
    ref.intData = intData1;
    ref.intData2 = intData2;
    ref.data = data;
}
