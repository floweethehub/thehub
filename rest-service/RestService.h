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
#ifndef RESTSERVICE_H
#define RESTSERVICE_H

#include <QJsonDocument>
#include <QJsonObject>

#include <Blockchain.h>
#include <httpengine/server.h>

class RestServiceWebRequest;

struct RequestString
{
    RequestString(const QString &path);

    QString anonPath() const;

    QString wholePath;
    QString request;
    QString argument; // only used for GET
    QJsonDocument post;
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

struct AnswerDataBase {
    virtual ~AnswerDataBase() = default;
};

class RestServiceWebRequest : public HttpEngine::WebRequest, public Blockchain::Search
{
    Q_OBJECT
public:
    RestServiceWebRequest(qintptr socketDescriptor, std::function<void(HttpEngine::WebRequest*)> &handler);
    ~RestServiceWebRequest() override;

    enum {
        Unset,
        TransactionDetails,
        TransactionDetailsList,
        AddressDetails,
        AddressDetailsList,
        AddressUTXO,
        AddressUTXOList,
        GetRawTransaction,
        GetRawTransactionVerbose,
        SendRawTransaction,
    } answerType = Unset;

    // Blockchain::Search interface
    void finished(int unfinishedJobs) override;
    void transactionAdded(const Blockchain::Transaction &transaction, int answerIndex) override;
    void spentOutputResolved(int jobId, int blockHeight, int offsetInBlock) override;
    void addressUsedInOutput(int blockHeight, int offsetInBlock, int outIndex) override;
    void utxoLookup(int jobId, int blockHeight, int offsetInBlock, int outindex, bool unspent, int64_t amount, Streaming::ConstBuffer outputScript) override;
    void aborted(const Blockchain::ServiceUnavailableException &e) override;

    // data specific for an AnswerType
    AnswerDataBase *answerData = nullptr;

private slots:
    void threadSafeFinished();

private:
    QJsonObject renderTransactionToJSon(const Blockchain::Transaction &tx) const;
};

class RestService : public QObject, public Blockchain::SearchEngine
{
    Q_OBJECT
public:
    RestService();

    // http engine callback
    void onIncomingConnection(HttpEngine::WebRequest *request);

    void parseConfig(const std::string &confFile) override;

    void initializeHubConnection(NetworkConnection connection, const std::string &hubVersion) override;

public slots:
    void onReparseConfig();

private:
    void requestTransactionInfo(const RequestString &rs, RestServiceWebRequest *request);
    void requestAddressInfo(const RequestString &rs, RestServiceWebRequest *request);
    void requestRawTransaction(const RequestString &rs, RestServiceWebRequest *request);

private:
    void findServices(const QString &configFile);
};

#endif
