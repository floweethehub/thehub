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
#ifndef BITCOREPROXY_H
#define BITCOREPROXY_H

#include <QString>
#include <Blockchain.h>
#include <QJsonObject>
#include <httpengine/server.h>

#include <boost/unordered_map.hpp>

class BitCoreWebRequest;

struct RequestString
{
    RequestString(const QString &path);

    QString anonPath() const;

    QString wholePath;
    QString chain;
    QString network;
    QString request;
    QString post;
};

struct TxRef {
    TxRef(int blockHeight, int offsetInBlock)
        : blockHeight(blockHeight),
          offsetInBlock(offsetInBlock)
    {
    }
    int blockHeight = 0, offsetInBlock = 0;

    QMap<int, QPair<int, int> > spentOutputs; // output-index to blockHeight, offset pair
};

class BitcoreWebRequest : public HttpEngine::WebRequest, public Blockchain::Search
{
    Q_OBJECT
public:
    BitcoreWebRequest(qintptr socketDescriptor, std::function<void(HttpEngine::WebRequest*)> &handler);
    ~BitcoreWebRequest() override;

    enum {
        Unset,
        TxForHeight,
        TxForBlockHash,
        TxForTxId,
        TxForTxIdAuthHead,
        TxForTxIdCoins,

        AddressTxs,
        AddressUnspentOutputs,
        AddressBalance,
    } answerType = Unset;

    QJsonObject &map();

    // Blockchain::Search interface
    void finished(int unfinishedJobs) override;
    void transactionAdded(const Blockchain::Transaction &transaction) override;
    void txIdResolved(int jobId, int blockHeight, int offsetInBlock) override;
    void spentOutputResolved(int jobId, int blockHeight, int offsetInBlock) override;
    void addressUsedInOutput(int blockHeight, int offsetInBlock, int outIndex) override;
    void utxoLookup(int jobId, int blockHeight, int offsetInBlock, int outIndex, bool unspent, int64_t amount, Streaming::ConstBuffer outputScript) override;

    QJsonObject m_map;

private slots:
    void threadSafeFinished();

private:
    // add things like 'network', 'chain' and '_id'
    void addDefaults(QJsonObject &node);

    boost::unordered_map<uint256, int, HashShortener> blockHeights;

    // remembers the transactions we looked up and the outpoints we were interested in and who spent those.
    // key: pair of blockHeight to offsetInBlock (aka transaction)
    // value: map of outindex to a pair indicating the spending transaction
    std::map<std::pair<int,int>, std::map<int, std::pair<int, int>> > txRefs;

#ifdef BENCH
    QDateTime startTime;
#endif
};

class BitcoreProxy : public QObject, public Blockchain::SearchEngine
{
    Q_OBJECT
public:
    BitcoreProxy();

    // http engine callback
    void onIncomingConnection(HttpEngine::WebRequest *request);

    void parseConfig(const std::string &confFile) override;

    void initializeHubConnection(NetworkConnection connection, const std::string &hubVersion) override;

public slots:
    void onReparseConfig();

private:
    void returnEnabledChains(HttpEngine::WebRequest *request) const;
    void returnTemplatePath(HttpEngine::Socket *socket, const QString &templateName, const QString &error = QString()) const;

    void requestTransactionInfo(const RequestString &rs, BitcoreWebRequest *request);
    void requestAddressInfo(const RequestString &rs, BitcoreWebRequest *request);
    void requestBlockInfo(const RequestString &rs, BitcoreWebRequest *request);
    void returnFeeSuggestion(const RequestString &rs, BitcoreWebRequest *request);
    void returnDailyTransactions(const RequestString &rs, BitcoreWebRequest *request);

private:
    void findServices(const QString &configFile);
};

#endif
