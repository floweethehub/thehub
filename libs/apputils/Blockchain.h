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
#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <NetworkConnection.h>
#include <NetworkEndPoint.h>
#include <uint256.h>
#include <Message.h>
#include <streaming/BufferPool.h>

#include <vector>
#include <map>
#include <deque>
#include <mutex>

namespace Blockchain
{
enum Service {
    TheHub,
    IndexerTxIdDb,
    IndexerAddressDb,
    IndexerSpentDb
};

enum JobType {
    Unset,
    LookupTxById,
    LookupByAddress,
    LookupSpentTx,

    FetchTx = 16,
    FetchBlockHeader,
    FetchBlockOfTx,
    FetchUTXOUnspent,
    FetchUTXODetails,
    FindTxInMempool,
    FindAddressInMempool,

    CustomHubMessage = 100, // the data field is a Message to send to Hub.
};

struct Job {
    JobType type = Unset;
    bool started = false, finished = false;
    int nextJobId = -1, nextJobId2 = -1;
    uint32_t transactionFilters = 1; ///< see TransactionFilter enum
    int intData = 0, intData2 = 0, intData3 = 0;
    Streaming::ConstBuffer data;
};

class ServiceUnavailableException : public std::runtime_error
{
public:
    explicit ServiceUnavailableException(const char *error, Service service, bool temporarily);

    Service service() const {
        return m_service;
    }
    bool temporarily() const {
        return m_temporarily;
    }

    const Service m_service;
    const bool m_temporarily;
};

class SearchPolicy;
class SearchEnginePrivate;

struct Input
{
    Streaming::ConstBuffer prevTxId, inputScript;
    int outIndex = -1;
};

struct Output
{
    uint64_t amount = 0;
    short index = -1;
    enum OutScriptType {
        Nothing,
        FullScript,
        OnlyAddress
    } type = Nothing;
    Streaming::ConstBuffer outScript;
};

struct Transaction
{
    int blockHeight = -1;
    union {
        // offsetInBlock is used when blockHeight is valid
        int offsetInBlock;
        uint32_t firstSeenTime = 0;
    };
    int jobId = -1; // jobId that was processed to create this object
    short outIndex = -1; // if >= 0, the outputIndex returned in a search
    int fees = -1;

    bool isCoinbase() const {
        return offsetInBlock > 0 && offsetInBlock < 100;
    }
    Streaming::ConstBuffer fullTxData, txid;

    std::vector<Input> inputs;
    std::vector<Output> outputs;

    // For clients that want this to be populated: when you fetch a transaction
    // based on the search result of the spentDb, make sure that the 'job'
    // key]  bool isOutput, index -> (isOutput ? 1 << 25: 0) + (index & 0xFFFFFF);
    std::map<uint32_t, Transaction*> txRefs;
    static uint32_t refKeyForInput(int i) {
        assert(i >= 0);
        assert(i < 0x1000000);
        return 0x1000000 + static_cast<uint32_t>(i);
    }
    static uint32_t refKeyForOutput(int i) {
        assert(i >= 0);
        assert(i < 0x1000000);
        return static_cast<uint32_t>(i);
    }
};

struct BlockHeader
{
    Streaming::ConstBuffer hash, merkleRoot;
    int confirmations = -1, height = 0;
    uint32_t version = 0, time = 0, medianTime = 0, nonce = 0, bits = 0;
    double difficulty = 0;
};

enum TransactionFilter {
    IncludeOffsetInBlock = 1,
    IncludeInputs = 2,
    IncludeTxId = 4,
    IncludeFullTransactionData = 8,
    IncludeOutputs = 0x10,
    IncludeOutputAmounts = 0x20,
    IncludeOutputScripts = 0x40,
    IncludeOutputAddresses = 0x80,
    IncludeOutputScriptHash = 0x100,
    IncludeTxFees = 0x200
};

struct Error {
    int serviceId = -1;
    int messageId = -1;
    std::string error;
};

struct Search
{
public:
    Search() {}
    virtual ~Search();

    /**
     * @brief finished is called when no more jobs can be started.
     * @param unfinishedJobs is the count of jobs that were defined but
     *        were not started due to missing information.
     *
     * A job is finished when we can't do anything more. In most cases that means
     * it has finished all jobs and the results are available.
     *
     * Poorly set up job-queues may have jobs that can't be started due to missing
     * data, we won't let those stop us from finishing and as such we can have
     * a non-zero unfinishedJobs count.
     */
    virtual void finished(int unfinishedJobs) {}

    /**
     * @brief transactionAdded is called when a transaction was retrieved.
     * @param transaction
     * @param answerIndex the index in the list of Answers for this transaction.
     *
     * Many jobs end up fetching a transaction from remote, while you can wait
     * until the entire job is finished, this callback allows you to parse the
     * transaction and add more jobs to the search object.
     */
    virtual void transactionAdded(const Transaction &transaction, int answerIndex) { }

    /**
     * @brief txIdResolved is called when the Indexer resolved a txid.
     * @param jobId the job-index that requested the lookup of the txid.
     * @param blockHeight the resulting blockheight
     * @param offsetInBlock the resulting offsetInBlock
     */
    virtual void txIdResolved(int jobId, int blockHeight, int offsetInBlock) { }

    /**
     * @brief spentOutputResolved is called when the indexer resolved who spent an output.
     * @param jobId the job-index that requested the lookup of the txid + out-index.
     * @param blockHeight the resulting blockheight
     * @param offsetInBlock the resulting offsetInBlock
     */
    virtual void spentOutputResolved(int jobId, int blockHeight, int offsetInBlock) { }

    /**
     * @brief addressUsedInOutput is called to list transactions that pay to a certain address.
     * @param blockHeight the resulting blockheight
     * @param offsetInBlock the resulting offsetInBlock
     * @param outIndex the resulting output-index
     *
     * A LookupByAddress type job will find all transaction-outputs that send money to a certain
     * address. The resulting items are passed into this method.
     */
    virtual void addressUsedInOutput(int blockHeight, int offsetInBlock, int outIndex) { }

    /**
     * @brief utxoLookup is called when a utxo lookup returns.
     *
     * The UTXO lookup-request is in most cases requested based on a blockheight, offsetinblock and outindex.
     * In this call those 3 values are repeated, then followed with the result.
     *
     * In case of a request FetchUTXOUnspent, the only relevant value is \a unspent.
     * In case of a request FetchUTXODetails the amount and outscript are also provided.
     *
     * @param jobId the originating JobId
     * @param blockHeight Copy of height from the request.
     * @param offsetInBlock Copy of offsetInBlock from the request.
     * @param outIndex copy of the outIndex from the request.
     * @param unspent a bool stating that the UTXO is as of yet unspent.
     * @param amount if FetchUTXODetails was used, and unspent was true then the amount. Otherwise -1
     * @param outputScript if FetchUTXODetails was used, and unspent was true then the script. Otherwise empty.
     */
    virtual void utxoLookup(int jobId, int blockHeight, int offsetInBlock, int outIndex, bool unspent, int64_t amount, Streaming::ConstBuffer outputScript) { }

    virtual void aborted(const ServiceUnavailableException&) = 0;

    // used by the engine to ID the request, set and used only by the engine.
    int requestId = -1;

    // questions
    std::mutex jobsLock;
    std::deque<Job> jobs;

    // results
    std::deque<Transaction> answer;
    std::map<int, BlockHeader> blockHeaders;
    std::map<int, Error> errors;

    // set by the SearchEngine in SearchEngine::start()
    SearchPolicy *policy = nullptr;

    /*
     * Any transaction has unknown data about its inputs and outputs. It is common
     * to fetch the transaction on the other side of either that input or output.
     * This helps.
     * Clients that create a `job.type = Blockchain::FetchTx` should insert a row
     * into the txRef with the new jobId (the FetchTX) -> the output of the txRefKey()
     * then this Search class will automatically match the transaction when (if) it comes
     * in and update your Transaction::txRef member
     */
    enum class TxRef { Input, Output };
    static uint64_t txRefKey(int origTxIndex, TxRef ref, int index);
    std::map<int, uint64_t> txRefs;
};

class SearchEngine
{
public:
    SearchEngine();
    virtual ~SearchEngine();

    /// start processing a search
    /// throws ServiceUnavailableException
    void start(Search *request);

    void addIndexer(const EndPoint &ep);
    void addHub(const EndPoint &ep);

    void setConfigFile(const std::string &configFile);
    virtual void parseConfig(const std::string &confFile);

    bool isHubConnected() const;
    bool isIndexerConnected() const;

    virtual void initializeHubConnection(NetworkConnection connection, const std::string &hubVersion) { }
    virtual void initializeIndexerConnection(NetworkConnection connection, const std::set<Service> &services) { }
    virtual void hubSentMessage(const Message &message) { }
    virtual void indexerSentMessage(const Message &message) { }
    virtual void hubDisconnected() {}
    virtual void indexerDisconnected() {}

    SearchEngine(const SearchEngine &) = delete;
    SearchEngine &operator=(SearchEngine&) = delete;
protected:
    void reparseConfig();

    Streaming::BufferPool &poolForThread(int reservation);
    void sendMessage(const Message &message, Service service);

    SearchEnginePrivate *d;
};

};

#endif
