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
#ifndef TESTAPI_H
#define TESTAPI_H

#include <QJsonValue>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>


class TestApi : public QObject
{
    Q_OBJECT
public:
    TestApi();

    void start(const QString &hostname, int port);

    QString hostname() const;
    int port() const;

public slots:
    void finishedRequest();

private:
    QNetworkAccessManager m_network;
    QString m_hostname;
    int m_port = -1;
    int m_finishedRequests = 0;
};


class AbstractTestCall : public QObject
{
    Q_OBJECT
public:
    enum CallType {
        POST,
        GET
    };
protected:
    AbstractTestCall(QNetworkReply *parent, CallType callType = GET);

    virtual void checkDocument(const QJsonDocument &doc) = 0;

    void startContext(const QString &context);
    void error(const QString &error);

    template<class V>
    inline void check(const QJsonValue &o, const QString &key, V value) {
        if (o.isNull()) return;
        if (o[key] == value)
            return;
        if (o[key] == QJsonValue::Undefined) {
            static const QString failure1("%1 missing");
            error(failure1.arg(key));
        }
        else {
            static const QString failure2("%1 has incorrect value");
            error(failure2.arg(key));
        }
    }
    template<class V>
    inline void check(const QJsonArray &o, int index, V value) {
        if (o.at(index) == value)
            return;
        static const QString failure3("array[%1] has incorrect value");
        error(failure3.arg(index));
    }

    inline QJsonArray checkArray(const QJsonValue &o, const QString &key, int size) {
        auto a = o[key];
        if (a.isNull()) {
            error("Missing array: " + key);
            return QJsonArray();
        }
        if (!a.isArray()) {
            error("Not an array: " + key);
            return QJsonArray();
        }
        auto aa = a.toArray();
        if (aa.size() != size)
            error("Array not expected length: " + key);
        return aa;
    }
    inline QJsonObject checkProp(const QJsonValue &o, const QString &key) {
        auto a = o[key];
        if (a.isNull()) {
            error("Missing property: " + key);
            return QJsonObject();
        }
        if (!a.isObject()) {
            error("Property is not an object: " + key);
            return QJsonObject();
        }
        return a.toObject();
    }

signals:
    void requestDone();

private slots:
    void finished();
    void timeout();

protected:
    QNetworkReply *m_reply;
    struct Error {
        QString context;
        QString error;
    };

    QList<Error> m_errors;
    QString m_context;

    const CallType m_callType;
};


class TestAddressDetails : public AbstractTestCall
{
    Q_OBJECT
public:
    static void startRequest(TestApi *parent, QNetworkAccessManager &manager, CallType type);
    static QByteArray s_postData;

protected:
    TestAddressDetails(QNetworkReply *parent, CallType ct) : AbstractTestCall(parent, ct) { }

    void checkDocument(const QJsonDocument &doc) override;
};

class TestAddressDetails2 : public AbstractTestCall
{
    Q_OBJECT
public:
    static void startRequest(TestApi *parent, QNetworkAccessManager &manager);

protected:
    TestAddressDetails2(QNetworkReply *parent) : AbstractTestCall(parent) { }

    void checkDocument(const QJsonDocument &doc) override;
};

class TestAddressUTXO : public AbstractTestCall
{
    Q_OBJECT
public:
    static void startRequest(TestApi *parent, QNetworkAccessManager &manager);

protected:
    TestAddressUTXO(QNetworkReply *parent) : AbstractTestCall(parent) { }

    void checkDocument(const QJsonDocument &doc) override;
};

class TestAddressUTXOPost : public AbstractTestCall
{
    Q_OBJECT
public:
    static void startRequest(TestApi *parent, QNetworkAccessManager &manager);
    static QByteArray s_postData;

protected:
    TestAddressUTXOPost(QNetworkReply *parent) : AbstractTestCall(parent, POST) { }

    void checkDocument(const QJsonDocument &doc) override;
};

class TestTransactionDetails : public AbstractTestCall
{
    Q_OBJECT
public:
    static void startRequest(TestApi *parent, QNetworkAccessManager &manager);

protected:
    TestTransactionDetails(QNetworkReply *parent, CallType type = GET) : AbstractTestCall(parent, type) { }

    void checkDocument(const QJsonDocument &doc) override;
    void checkDetails221fd0f3(const QJsonObject &tx);
    void checkDetails221fd0f3_more(const QJsonObject &tx);
};

class TestTransactionDetailsPost : public TestTransactionDetails
{
    Q_OBJECT
public:
    static void startRequest(TestApi *parent, QNetworkAccessManager &manager);
    static QByteArray s_postData;

protected:
    TestTransactionDetailsPost(QNetworkReply *parent) : TestTransactionDetails(parent, POST) { }

    void checkDocument(const QJsonDocument &doc) override;

};

class GetRawTransactionVerbose : public TestTransactionDetails
{
    Q_OBJECT
public:
    static void startRequest(TestApi *parent, QNetworkAccessManager &manager);

protected:
    GetRawTransactionVerbose(QNetworkReply *parent, CallType type = GET) : TestTransactionDetails(parent, type) { }

    void checkDocument(const QJsonDocument &doc) override;
};

class GetRawTransaction : public QObject
{
    Q_OBJECT
public:
    static void startRequest(TestApi *parent, QNetworkAccessManager &manager);
    GetRawTransaction(QNetworkReply *parent);

signals:
    void requestDone();

private slots:
    void finished();
    void timeout();

private:
    QNetworkReply *m_reply;
};

class SendRawTransaction : public AbstractTestCall
{
    Q_OBJECT
public:
    static void startRequest(TestApi *parent, QNetworkAccessManager &manager, CallType type = GET);

protected:
    SendRawTransaction(QNetworkReply *parent, CallType type = GET) : AbstractTestCall(parent, type) { }

    void checkDocument(const QJsonDocument &doc) override;
};

/*
 * API mapping to the functions testing them.
 *
 * GET /address/details/{address}
 * 		TestAddressDetails
 * 		TestAddressDetails2
 * POST /address/details
 * 		TestAddressDetails
 * GET /address/utxo/{address}
 * 		TestAddressUTXO
 * POST /address/utxo
 * 		TestAddressUTXOPost
 * GET /address/unconfirmed/{address}
 * POST /address/unconfirmed
 * GET /address/transactions/{address}
 * POST /address/transactions
 * GET /address/fromXPub/{xpub}
 *
 * GET /block/detailsByHash/{hash}
 * POST /block/detailsByHash
 * GET /block/detailsByHeight/{height}
 * POST /block/detailsByHeight
 *
 * GET /blockchain/getBestBlockHash
 * GET /blockchain/getBlockchainInfo
 * GET /blockchain/getBlockCount
 * GET /blockchain/getBlockHeader/{hash}
 * POST /blockchain/getBlockHeader
 * GET /blockchain/getChainTips
 * GET /blockchain/getDifficulty
 * GET /blockchain/getMempoolEntry/{txid}
 * POST /blockchain/getMempoolEntry
 * GET /blockchain/getMempoolInfo
 * GET /blockchain/getRawMempool
 * GET /blockchain/getTxOut/{txid}/{n}
 * GET /blockchain/getTxOutProof/{txid}
 * POST /blockchain/getTxOutProof
 * GET /blockchain/verifyTxOutProof/{proof}
 * POST /blockchain/verifyTxOutProof
 *
 * GET /control/getInfo
 * GET /control/getNetworkInfo
 *
 * GET /mining/getMiningInfo
 * GET /mining/getNetworkHashps
 *
 * GET /rawtransactions/decodeRawTransaction/{hex}
 * POST /rawtransactions/decodeRawTransaction
 * GET /rawtransactions/decodeScript/{hex}
 * POST /rawtransactions/decodeScript
 * GET /rawtransactions/getRawTransaction/{txid}
 *  	GetRawTransaction
 *  	GetRawTransactionVerbose
 * POST /rawtransactions/getRawTransaction
 * GET /rawtransactions/sendRawTransaction/{hex}
 * 		SendRawTransaction
 * POST /rawtransactions/sendRawTransaction
 *
 * GET /transaction/details/{txid}
 * 		TestTransactionDetails
 * POST /transaction/details
 *
 * GET /util/validateAddress/{address}
 * POST /util/validateAddress
*/

#endif
