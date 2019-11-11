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
#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <QString>
#include <QSettings>

#include <NetworkConnection.h>
#include <NetworkEndPoint.h>
#include <uint256.h>
#include <Message.h>

#include <boost/unordered_map.hpp>

#include <vector>
#include <deque>

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
    FetchUTXODetails
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
    explicit ServiceUnavailableException(const char *error, Service service);

    Service service() const {
        return m_service;
    }

    const Service m_service;
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
    int offsetInBlock = 0;
    int jobId = -1; // jobId that was processed to create this object

    bool isCoinbase() const {
        return blockHeight > 0 && blockHeight < 100;
    }
    Streaming::ConstBuffer fullTxData, txid;

    std::vector<Input> inputs;
    std::vector<Output> outputs;
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
    IncludeOutputAddresses = 0x80
};

struct Search
{
public:
    Search() {}
    virtual ~Search();

    virtual void finished(int unfinishedJobs) { Q_UNUSED(unfinishedJobs) }
    virtual void dataAdded(const Message &message) { Q_UNUSED(message) }
    virtual void transactionAdded(const Transaction &transaction) {
        Q_UNUSED(transaction)
    }
    virtual void txIdResolved(int jobId, int blockHeight, int offsetInBlock) {
        Q_UNUSED(jobId)
        Q_UNUSED(blockHeight)
        Q_UNUSED(offsetInBlock)
    }
    /**
     * The job \a jobId returned and the indexer returned the height+offset.
     */
    virtual void spentOutputResolved(int jobId, int blockHeight, int offsetInBlock) {
        Q_UNUSED(jobId)
        Q_UNUSED(blockHeight)
        Q_UNUSED(offsetInBlock)
    }
    virtual void addressUsedInOutput(int blockHeight, int offsetInBlock, int outIndex) {
        Q_UNUSED(blockHeight)
        Q_UNUSED(offsetInBlock)
        Q_UNUSED(outIndex)

    }
    virtual void utxoLookup(int blockHeight, int offsetInBlock, bool unspent) {
        Q_UNUSED(blockHeight)
        Q_UNUSED(offsetInBlock)
        Q_UNUSED(unspent)
    }

    // used by the engine to ID the request, set and used only by the engine.
    int requestId = -1;

    // questions
    std::deque<Job> jobs;

    // results
    boost::unordered_map<uint256, int, HashShortener> transactionMap;
    std::deque<Transaction> answer;
    std::map<int, BlockHeader> blockHeaders;
    int64_t answerAmount = -1;

    // set by the SearchEngine in SearchEngine::start()
    SearchPolicy *policy = nullptr;
};

class SearchEngine : public QObject
{
    Q_OBJECT
public:
    SearchEngine();
    ~SearchEngine();

    /// start processing a search
    /// throws ServiceUnavailableException
    void start(Search *request);

    /*
     * probable want to subclass Search to provide a blocking API and have these
     * methods return that.

    Search* findTransaction(int blockHeight, int offsetInBlock, Search *state = nullptr);
    Search* findTransaction(const uint256 &txid, Search *state = nullptr);

    Search* findTransactions(const uint160 &address, Search *state = nullptr);
    Search* findTransactions(const uint256 &txid, Search *state = nullptr);
    */

    void addIndexer(const EndPoint &ep);
    void addHub(const EndPoint &ep);

    void setConfigFile(const QString &configFile);

    virtual void parseConfig(const QString &confFile);

    virtual void initializeHubConnection(NetworkConnection connection, const std::string &hubVersion) {
        Q_UNUSED(connection)
        Q_UNUSED(hubVersion)
    }
    virtual void initializeIndexerConnection(NetworkConnection connection) { Q_UNUSED(connection) }
    virtual void hubSentMessage(const Message &message) { Q_UNUSED(message) }
    virtual void indexerSentMessage(const Message &message) { Q_UNUSED(message) }

public slots:
    void reparseConfig();

protected:
    SearchEnginePrivate *d;
};

};

#endif
