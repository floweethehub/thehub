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
#include "TestApi.h"

#include <FloweeServiceApplication.h>
#include <QNetworkReply>
#include <QMetaEnum>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>

TestApi::TestApi()
{
}

void TestApi::start(const QString &hostname, int port)
{
    m_port = port;
    m_hostname = hostname;
    if (m_hostname.isEmpty())
        m_hostname = QLatin1String("http://localhost");
    else if (m_port == 443)
        m_hostname = "https://" + m_hostname;
    else
        m_hostname = "http://" + m_hostname;

    // The Qt network access manager will serialize these since they all go to the same host.
    // We create all, but wait for one test to start (the timeout-timer) for the next.

    m_finishedRequests = 0;
    TestTxBlockHeight::startRequest(this, m_network);
}

QString TestApi::hostname() const
{
    return m_hostname;
}

int TestApi::port() const
{
    return m_port;
}

void TestApi::finishedRequest()
{
    switch (m_finishedRequests++) {
    case 0:
        TestTxBlockHash::startRequest(this, m_network);
        break;
    case 1:
        TestTx::startRequest(this, m_network);
        break;
    case 2:
        TestTx2::startRequest(this, m_network);
        break;
    case 3:
        TestTxCoins::startRequest(this, m_network);
        break;
    case 4:
        TestTxCoins2::startRequest(this, m_network);
        break;
    case 5:
        TestAddressTxs::startRequest(this, m_network);
        break;
    case 6:
        TestAddressOutputs::startRequest(this, m_network);
        break;
    case 7:
        TestAddressBalance::startRequest(this, m_network);
        break;
    case 8:
        QCoreApplication::quit();
    }

    // TODO
    // TestTxAuthHead::startRequest(this, m_network);
}


//////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
    FloweeServiceApplication app(argc, argv);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("bitcore-proxy-tester");

    QCommandLineParser parser;
    parser.setApplicationDescription("BitCore proxy test-client");
    parser.addHelpOption();
    app.addClientOptions(parser);
    parser.process(app.arguments());

    app.setup();

    logFatal() << "Starting";
    TestApi tester;
    auto server =app.serverAddressFromArguments(3000);
    tester.start(QString::fromStdString(server.hostname), server.announcePort);

    return app.exec();
}


//////////////////////////////////////////////////////////////////

AbstractTestCall::AbstractTestCall(QNetworkReply *parent)
    : QObject(parent),
      m_reply(parent)
{
    connect (m_reply, SIGNAL(finished()), this, SLOT(finished()));
    QTimer::singleShot(10000, this, SLOT(timeout()));
}

void AbstractTestCall::startContext(const QString &context)
{
    m_context = context;
}

void AbstractTestCall::error(const QString &error) {
    m_errors.append({m_context, error});
}

void AbstractTestCall::finished()
{
    Q_ASSERT(metaObject()->className() != QString("AbstractTestCall"));
    logCritical().nospace() << metaObject()->className() + 4 << " [" << m_reply->url().toString() << "]";
    for (auto x : m_reply->rawHeaderList()) {
        logInfo().nospace() << "  " << QString::fromLatin1(x) << ": " << QString::fromUtf8(m_reply->rawHeader(x));
    }
    if (m_reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(m_reply->readAll());
        if (doc.isNull())
            error("  document could not be parsed, is it JSON?");
        else
            checkDocument(doc);

        if (m_errors.isEmpty()) {
            logCritical() << "  ✓ all Ok";
        } else {
            for (auto e : m_errors) {
                if (e.context.isEmpty())
                    logFatal() << "  ❎" << e.error;
                else
                    logFatal() << "  ❎" << e.context << "|" << e.error;
            }
        }
    }
    else {
        logFatal() << "  ❎" << m_reply->errorString();
    }

    deleteLater();
    emit requestDone();
}

void AbstractTestCall::timeout()
{
    logCritical() << m_reply->url().toString();
    logCritical() << "  ❎ Request never returned";
    deleteLater();
    emit requestDone();
}


//////////////////////////////////////////////////////////

void TestTxBlockHeight::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString blockHeight("%1:%2/api/BCH/mainnet/tx?blockHeight=12");
    auto reply = manager.get(QNetworkRequest(blockHeight.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestTxBlockHeight(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestTxBlockHeight::checkDocument(const QJsonDocument &doc)
{
    if (!doc.isArray())
        error("Root should be an array");
    auto root = doc.array();
    if (root.count() != 1)
        error("root-array has incorrect number of items");
    auto tx = root.at(0);
    if (!tx.isObject())
        error("Tx should be an object");
    check(tx, "blockHash", "0000000027c2488e2510d1acf4369787784fa20ee084c258b58d9fbd43802b5e");
    check(tx, "blockTimeNormalized", "2009-01-09T04:21:28.000Z");
    check(tx, "blockHeight", 12);
    check(tx, "chain", "BCH");
    check(tx, "coinbase", true);
    if (!tx["confirmations"].isDouble())
        error("confirmations should be there and be a number");
    check(tx, "inputCount", 1);
    check(tx, "size", 134);
    check(tx, "txid", "3b96bb7e197ef276b85131afd4a09c059cc368133a26ca04ebffb0ab4f75c8b8");
    check(tx, "value", (qint64) 5000000000);
    check(tx, "locktime", -1);
    check(tx, "fee", -1);
}

void TestTxBlockHash::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString blockHeight("%1:%2/api/BCH/mainnet/tx?blockHash=0000000027c2488e2510d1acf4369787784fa20ee084c258b58d9fbd43802b5e");
    auto reply = manager.get(QNetworkRequest(blockHeight.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestTxBlockHash(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestTx::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString blockHeight("%1:%2/api/BCH/mainnet/tx/3b96bb7e197ef276b85131afd4a09c059cc368133a26ca04ebffb0ab4f75c8b8");
    auto reply = manager.get(QNetworkRequest(blockHeight.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestTx(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestTx::checkDocument(const QJsonDocument &doc)
{
    if (!doc.isObject())
        error("Root should be an object, not an array");
    if (doc["inputs"] != QJsonValue::Undefined)
        error("inputs not expected but present");
    if (doc["outputs"] != QJsonValue::Undefined)
        error("outputs not expected but present");

    QJsonObject coin = doc.object();
    check(coin, "txid", "3b96bb7e197ef276b85131afd4a09c059cc368133a26ca04ebffb0ab4f75c8b8");
    check(coin, "chain", "BCH");
    check(coin, "network", "mainnet");
    check(coin, "blockHeight", 12);
    check(coin, "blockHash", "0000000027c2488e2510d1acf4369787784fa20ee084c258b58d9fbd43802b5e");
    check(coin, "blockTime", "2009-01-09T04:21:28.000Z");
    check(coin, "blockTimeNormalized",  "2009-01-09T04:21:28.000Z");
    check(coin, "coinbase", true);
    check(coin, "locktime", -1);
    check(coin, "inputCount", 1);
    check(coin, "outputCount", 1);
    check(coin, "size", 134);
    check(coin, "fee", -1);
    check(coin, "value", (qint64) 5000000000);
}

void TestTx2::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString blockHeight("%1:%2/api/BCH/mainnet/tx/609ea5cb7dd5ae908aaea2bf5a98cc7bb45b85b6e43c6d1dee48f5179ca8efa8");
    auto reply = manager.get(QNetworkRequest(blockHeight.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestTx2(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestTx2::checkDocument(const QJsonDocument &doc)
{
    if (!doc.isObject())
        error("Root should be an object, not an array");
    if (doc["inputs"] != QJsonValue::Undefined)
        error("inputs not expected but present");
    if (doc["outputs"] != QJsonValue::Undefined)
        error("outputs not expected but present");

    QJsonObject coin = doc.object();
    check(coin, "txid", "609ea5cb7dd5ae908aaea2bf5a98cc7bb45b85b6e43c6d1dee48f5179ca8efa8");
    check(coin, "chain", "BCH");
    check(coin, "network", "mainnet");
    check(coin, "blockHeight", 613042);
    check(coin, "blockHash", "00000000000000000057435d2d30474c6c100becff78ff996648caecf8a5f292");
    check(coin, "blockTime", "2019-12-12T15:36:09.000Z");
    check(coin, "blockTimeNormalized",  "2019-12-12T15:36:09.000Z");
    check(coin, "coinbase", false);
    check(coin, "locktime", 613040);
    check(coin, "inputCount", 3);
    check(coin, "outputCount", 2);
    check(coin, "size", 520);
    check(coin, "fee", 522);
    check(coin, "value", (qint64) 22646675);
}

void TestTxAuthHead::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString blockHeight("%1:%2/api/BCH/mainnet/tx/3b96bb7e197ef276b85131afd4a09c059cc368133a26ca04ebffb0ab4f75c8b8/authhead");
    auto reply = manager.get(QNetworkRequest(blockHeight.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestTxAuthHead(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestTxAuthHead::checkDocument(const QJsonDocument &doc)
{
     // TODO
}

void TestTxCoins::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString blockHeight("%1:%2/api/BCH/mainnet/tx/3b96bb7e197ef276b85131afd4a09c059cc368133a26ca04ebffb0ab4f75c8b8/coins");
    auto reply = manager.get(QNetworkRequest(blockHeight.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestTxCoins(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestTxCoins::checkDocument(const QJsonDocument &doc)
{
    if (!doc.isObject())
        error("Root should be an object, not an array");
    if (doc["inputs"] == QJsonValue::Undefined)
        error("Missing inputs root item");
    else {
        auto inputs = doc["inputs"].toArray();
        if (inputs.size() > 0)
            error("Coinbase should have no inputs");
    }
    if (doc["outputs"] == QJsonValue::Undefined) {
        error("Missing outputs root item");
    }
    else if (!doc["outputs"].isArray()) {
        error("outputs should be an array");
    } else {
        auto outputs = doc["outputs"].toArray();
        if (outputs.size() != 1)
            error("Incorrect number of coins");

        auto coin = outputs.at(0);
        check(coin, "address", "qrmn4jkcpxtqa0sp99jrswccfawffglnhgd2tf947a");
        check(coin, "chain", "BCH");
        check(coin, "network", "mainnet");
        check(coin, "coinbase", true);
        check(coin, "confirmations", -1);
        check(coin, "mintHeight", 12);
        check(coin, "spentHeight", -2);
        check(coin, "mintIndex", 0);
        check(coin, "spentTxid", "");
        check(coin, "script", "410478ebe2c28660cd2fa1ba17cc04e58d6312679005a7cad1fd56a7b7f4630bd700bcdb84a888a43fe1a2738ea1f3d2301d02faef357e8a5c35a706e4ae0352a6adac");
        check(coin, "mintTxid", "3b96bb7e197ef276b85131afd4a09c059cc368133a26ca04ebffb0ab4f75c8b8");
        check(coin, "value", (qint64) 5000000000);
    }
}

void TestTxCoins2::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString blockHeight("%1:%2/api/BCH/mainnet/tx/dedabaa2b1e6e5fff513bf0a2aeebccf2b650617ff540e4baa27ff3588692acc/coins");
    auto reply = manager.get(QNetworkRequest(blockHeight.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestTxCoins2(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestTxCoins2::checkDocument(const QJsonDocument &doc)
{
    if (!doc.isObject())
        error("Root should be an object, not an array");
    if (doc["inputs"] == QJsonValue::Undefined) {
        error("Missing inputs root item");
    } else if (!doc["inputs"].isArray()) {
        error("inputs should be an array");
    } else {
        auto inputs = doc["inputs"].toArray();
        if (inputs.size() != 1)
            error("Incorrect number of coins");

        startContext("in");
        auto in = inputs.at(0);
        check(in, "chain", "BCH");
        check(in, "network", "mainnet");
        check(in, "coinbase", false);
        check(in, "mintIndex", 0);
        check(in, "spentTxid", "dedabaa2b1e6e5fff513bf0a2aeebccf2b650617ff540e4baa27ff3588692acc");
        check(in, "mintTxid", "1a7482a97b77f11d9d6b903512143a20a61a8bc84e2d5b9ff9552ee5eb76c1ca");
        check(in, "confirmations", -1);
        check(in, "mintHeight", 119999);
        check(in, "spentHeight", 120000);
        check(in, "script", "76a914bd9df061f893b011d1640104c2fd817039d0596388ac");
        check(in, "address", "qz7emurplzfmqyw3vsqsfshas9crn5zevv4ha5zcpx");
        check(in, "value", (qint64) 2078000000);
    }
    if (doc["outputs"] == QJsonValue::Undefined) {
        error("Missing outputs root item");
    }
    else if (!doc["outputs"].isArray()) {
        error("outputs should be an array");
    } else {
        auto outputs = doc["outputs"].toArray();
        if (outputs.size() != 2)
            error("Incorrect number of coins");

        startContext("out/1");
        auto coin = outputs.at(0);
        check(coin, "chain", "BCH");
        check(coin, "network", "mainnet");
        check(coin, "coinbase", false);
        check(coin, "mintIndex", 0);
        check(coin, "spentTxid", "d81a57980bfcec9989e34f85d4c1e8905b940ea0d13949242a2de720d0b5b592");
        check(coin, "mintTxid", "dedabaa2b1e6e5fff513bf0a2aeebccf2b650617ff540e4baa27ff3588692acc");
        check(coin, "mintHeight", 120000);
        check(coin, "spentHeight", 120000);
        check(coin, "address", "qzv0q0gzuxsgu6q08g45nynye77e5pf7pyyckg382y");
        check(coin, "script", "76a91498f03d02e1a08e680f3a2b499264cfbd9a053e0988ac");
        check(coin, "confirmations", -1);
        check(coin, "value", (qint64) 1913000000);

        startContext("out/2");
        coin = outputs.at(1);
        check(coin, "chain", "BCH");
        check(coin, "network", "mainnet");
        check(coin, "coinbase", false);
        check(coin, "mintIndex", 1);
        check(coin, "spentTxid", "d6e9a30346bea29fd6352f34273c971f6b3615f4c4e5912be210d61073e210d1");
        check(coin, "mintTxid", "dedabaa2b1e6e5fff513bf0a2aeebccf2b650617ff540e4baa27ff3588692acc");
        check(coin, "mintHeight", 120000);
        check(coin, "spentHeight", 132894);
        check(coin, "address", "qpzyy54tcur68pp8drfdhjpqpxdjfquqh5vs35c4r8");
        check(coin, "script", "76a914444252abc707a3842768d2dbc820099b248380bd88ac");
        check(coin, "confirmations", -1);
        check(coin, "value", (qint64) 165000000);
    }
}

void TestAddressTxs::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString addressTxs("%1:%2/api/BCH/mainnet/address/qruexuvmqwc0cd7padx2qhvrdlmygefdnv2cqjpvq6/txs");
    auto reply = manager.get(QNetworkRequest(addressTxs.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestAddressTxs(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestAddressTxs::checkDocument(const QJsonDocument &doc)
{
    if (!doc.isArray()) {
        error("Root should be an array, not an object");
        return;
    }
    auto txs = doc.array();
    if (txs.size() != 10)
        error("Incorrect number of txs");

    startContext("tx1");
    auto in = txs.at(0);
    check(in, "chain", "BCH");
    check(in, "network", "mainnet");
    check(in, "coinbase", false);
    check(in, "mintIndex", 1);
    check(in, "spentTxid", "027e6a10e209a1cd16f2fbaa44c4c4da131fc9b58d228fe8b8e852f08d96df96");
    check(in, "mintTxid", "64dc2d189afdc07e65ef60ae80646769c1edf58dcdd05e4556d8291af33964e0");
    check(in, "confirmations", -1);
    check(in, "mintHeight", 229375);
    check(in, "spentHeight", 229612);
    check(in, "script", "76a914f993719b03b0fc37c1eb4ca05d836ff644652d9b88ac");
    check(in, "address", "qruexuvmqwc0cd7padx2qhvrdlmygefdnv2cqjpvq6");
    check(in, "value", (qint64) 2737777);

    startContext("tx2");
    in = txs.at(1);
    check(in, "chain", "BCH");
    check(in, "network", "mainnet");
    check(in, "coinbase", false);
    check(in, "mintIndex", 1);
    check(in, "spentTxid", "71a63a90a8deb304fcd7a225329ebee1c84f9bf49ecd156f88c9c845df4276bc");
    check(in, "mintTxid", "c924a04c7086d7c91f4e9498f389b474c5b911e4eab1467ac73af44d17999fcd");
    check(in, "confirmations", -1);
    check(in, "mintHeight", 136196);
    check(in, "spentHeight", 136198);
    check(in, "script", "76a914f993719b03b0fc37c1eb4ca05d836ff644652d9b88ac");
    check(in, "address", "qruexuvmqwc0cd7padx2qhvrdlmygefdnv2cqjpvq6");
    check(in, "value", (qint64) 1000000);

    startContext("tx3");
    in = txs.at(2);
    check(in, "chain", "BCH");
    check(in, "chain", "BCH");
    check(in, "network", "mainnet");
    check(in, "coinbase", false);
    check(in, "mintIndex", 1);
    check(in, "spentTxid", "98d8d54bf22f1aba20653763359b7f0f3c386e95f7db525b48bba12f79b61fae");
    check(in, "mintTxid", "442d6116a9d1ab3616ad0f4a8b49bcfb305e285de8d6cafc9ddc86c1653136a4");
    check(in, "confirmations", -1);
    check(in, "mintHeight", 130844);
    check(in, "spentHeight", 131731);
    check(in, "script", "76a914f993719b03b0fc37c1eb4ca05d836ff644652d9b88ac");
    check(in, "address", "qruexuvmqwc0cd7padx2qhvrdlmygefdnv2cqjpvq6");
    check(in, "value", (qint64) 2000000);

    startContext("tx4");
    in = txs.at(3);
    check(in, "chain", "BCH");
    check(in, "chain", "BCH");
    check(in, "network", "mainnet");
    check(in, "coinbase", false);
    check(in, "mintIndex", 1);
    check(in, "spentTxid", "98d8d54bf22f1aba20653763359b7f0f3c386e95f7db525b48bba12f79b61fae");
    check(in, "mintTxid", "9de4539e1ddf1590a3000803ea23f5c06521fb12a52af00a1c1a9b12b1289025");
    check(in, "confirmations", -1);
    check(in, "mintHeight", 129751);
    check(in, "spentHeight", 131731);
    check(in, "script", "76a914f993719b03b0fc37c1eb4ca05d836ff644652d9b88ac");
    check(in, "address", "qruexuvmqwc0cd7padx2qhvrdlmygefdnv2cqjpvq6");
    check(in, "value", (qint64) 2000000);

    startContext("tx5");
    in = txs.at(4);
    check(in, "chain", "BCH");
    check(in, "chain", "BCH");
    check(in, "network", "mainnet");
    check(in, "coinbase", false);
    check(in, "mintIndex", 1);
    check(in, "spentTxid", "659ff8192ab24cf9d44b8038f110d8f56f0a0e1757c5b4993d43d162fc29f0e2");
    check(in, "mintTxid", "f301b2ae513c204447b4de50534628b94ade0f0102fa489d30e4d1147c802c85");
    check(in, "confirmations", -1);
    check(in, "mintHeight", 128371);
    check(in, "spentHeight", 128379);
    check(in, "script", "76a914f993719b03b0fc37c1eb4ca05d836ff644652d9b88ac");
    check(in, "address", "qruexuvmqwc0cd7padx2qhvrdlmygefdnv2cqjpvq6");
    check(in, "value", (qint64) 1000000);

    startContext("tx6");
    in = txs.at(5);
    check(in, "chain", "BCH");
    check(in, "chain", "BCH");
    check(in, "network", "mainnet");
    check(in, "coinbase", false);
    check(in, "mintIndex", 1);
    check(in, "spentTxid", "659ff8192ab24cf9d44b8038f110d8f56f0a0e1757c5b4993d43d162fc29f0e2");
    check(in, "mintTxid", "fd0a73093c671e787cf46e8bfc9f794b2ddbe38dadd60009571cf2ceac49033d");
    check(in, "confirmations", -1);
    check(in, "mintHeight", 128040);
    check(in, "spentHeight", 128379);
    check(in, "script", "76a914f993719b03b0fc37c1eb4ca05d836ff644652d9b88ac");
    check(in, "address", "qruexuvmqwc0cd7padx2qhvrdlmygefdnv2cqjpvq6");
    check(in, "value", (qint64) 1000000);

    startContext("tx7");
    in = txs.at(6);
    check(in, "chain", "BCH");
    check(in, "chain", "BCH");
    check(in, "network", "mainnet");
    check(in, "coinbase", false);
    check(in, "mintIndex", 1);
    check(in, "spentTxid", "659ff8192ab24cf9d44b8038f110d8f56f0a0e1757c5b4993d43d162fc29f0e2");
    check(in, "mintTxid", "455a4ccc23dbbe75789e4abe7fb516bce07f9c753b7d0066a049fa6170bb4951");
    check(in, "confirmations", -1);
    check(in, "mintHeight", 127835);
    check(in, "spentHeight", 128379);
    check(in, "script", "76a914f993719b03b0fc37c1eb4ca05d836ff644652d9b88ac");
    check(in, "address", "qruexuvmqwc0cd7padx2qhvrdlmygefdnv2cqjpvq6");
    check(in, "value", (qint64) 7000000);

    startContext("tx8");
    in = txs.at(7);
    check(in, "chain", "BCH");
    check(in, "chain", "BCH");
    check(in, "network", "mainnet");
    check(in, "coinbase", false);
    check(in, "mintIndex", 1);
    check(in, "spentTxid", "659ff8192ab24cf9d44b8038f110d8f56f0a0e1757c5b4993d43d162fc29f0e2");
    check(in, "mintTxid", "9168546209cfca11a8ac3a1f32213457d37ad6c5fdf568639de258d1c79f7d7e");
    check(in, "confirmations", -1);
    check(in, "mintHeight", 127025);
    check(in, "spentHeight", 128379);
    check(in, "script", "76a914f993719b03b0fc37c1eb4ca05d836ff644652d9b88ac");
    check(in, "address", "qruexuvmqwc0cd7padx2qhvrdlmygefdnv2cqjpvq6");
    check(in, "value", (qint64) 13000000);

    startContext("tx9");
    in = txs.at(8);
    check(in, "chain", "BCH");
    check(in, "chain", "BCH");
    check(in, "network", "mainnet");
    check(in, "coinbase", false);
    check(in, "mintIndex", 1);
    check(in, "spentTxid", "1552efb5838378ca5f5d3d55dda06757f4fc679a7cc431affa2b5247e9956502");
    check(in, "mintTxid", "bfc6148e1c420935fc21e4543dda4d7fefbe2828559c4109e06796afab2cf1f9");
    check(in, "confirmations", -1);
    check(in, "mintHeight", 125786);
    check(in, "spentHeight", 125896);
    check(in, "script", "76a914f993719b03b0fc37c1eb4ca05d836ff644652d9b88ac");
    check(in, "address", "qruexuvmqwc0cd7padx2qhvrdlmygefdnv2cqjpvq6");
    check(in, "value", (qint64) 3000000);

    startContext("tx10");
    in = txs.at(9);
    check(in, "chain", "BCH");
    check(in, "chain", "BCH");
    check(in, "network", "mainnet");
    check(in, "coinbase", false);
    check(in, "mintIndex", 1);
    check(in, "spentTxid", "1552efb5838378ca5f5d3d55dda06757f4fc679a7cc431affa2b5247e9956502");
    check(in, "mintTxid", "2d18a9a278ac1afe4bc458fd286fe576fdea722a969e9113da474cc967146b76");
    check(in, "confirmations", -1);
    check(in, "mintHeight", 125456);
    check(in, "spentHeight", 125896);
    check(in, "script", "76a914f993719b03b0fc37c1eb4ca05d836ff644652d9b88ac");
    check(in, "address", "qruexuvmqwc0cd7padx2qhvrdlmygefdnv2cqjpvq6");
    check(in, "value", (qint64) 21000000);
}

void TestAddressOutputs::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString addressTxs("%1:%2/api/BCH/mainnet/address/1PYELM7jXHy5HhatbXGXfRpGrgMMxmpobu/?unspent=true");
    auto reply = manager.get(QNetworkRequest(addressTxs.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestAddressOutputs(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestAddressOutputs::checkDocument(const QJsonDocument &doc)
{
    if (!doc.isArray()) {
        error("Root should be an array, not an object");
        return;
    }
    auto txs = doc.array();
    if (txs.size() != 3)
        error("Incorrect number of txs");

    startContext("out1");
    auto in = txs.at(0);
    check(in, "chain", "BCH");
    check(in, "network", "mainnet");
    check(in, "coinbase", false);
    check(in, "mintIndex", 1);
    check(in, "spentTxid", "");
    check(in, "address", "qrmn4jkcpxtqa0sp99jrswccfawffglnhgd2tf947a");
    check(in, "mintTxid", "7307aa053fee854a50e432e07f177fc0ab012f4b584daf02b5a81f71cb54a117");
    check(in, "confirmations", -1);
    check(in, "mintHeight", 427269);
    check(in, "spentHeight", -2);
    check(in, "script", "76a914f73acad809960ebe012964383b184f5c94a3f3ba88ac");
    check(in, "value", (qint64) 100000);

    startContext("out2");
    in = txs.at(1);
    check(in, "chain", "BCH");
    check(in, "network", "mainnet");
    check(in, "coinbase", false);
    check(in, "mintIndex", 1);
    check(in, "spentTxid", "");
    check(in, "address", "qrmn4jkcpxtqa0sp99jrswccfawffglnhgd2tf947a");
    check(in, "mintTxid", "ef3cbd9631b13794ae2a5b38ee33f987a0a681a616b455b8b8d1819894c8b329");
    check(in, "confirmations", -1);
    check(in, "mintHeight", 465282);
    check(in, "spentHeight", -2);
    check(in, "script", "76a914f73acad809960ebe012964383b184f5c94a3f3ba88ac");
    check(in, "value", (qint64) 12213);

    startContext("out3");
    in = txs.at(2);
    check(in, "chain", "BCH");
    check(in, "network", "mainnet");
    check(in, "coinbase", true);
    check(in, "mintIndex", 0);
    check(in, "spentTxid", "");
    check(in, "address", "qrmn4jkcpxtqa0sp99jrswccfawffglnhgd2tf947a");
    check(in, "mintTxid", "3b96bb7e197ef276b85131afd4a09c059cc368133a26ca04ebffb0ab4f75c8b8");
    check(in, "confirmations", -1);
    check(in, "mintHeight", 12);
    check(in, "spentHeight", -2);
    check(in, "script", "76a914f73acad809960ebe012964383b184f5c94a3f3ba88ac");
    check(in, "value", (qint64) 5000000000);
}

void TestAddressBalance::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString addressTxs("%1:%2/api/BCH/mainnet/address/1PYELM7jXHy5HhatbXGXfRpGrgMMxmpobu/balance");
    auto reply = manager.get(QNetworkRequest(addressTxs.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestAddressBalance(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestAddressBalance::checkDocument(const QJsonDocument &doc)
{
    if (doc.isArray()) {
        error("Root should be an object, not an array");
        return;
    }
    auto object = doc.object();
    check(object, "confirmed", (qint64) 5000112213);
    check(object, "unconfirmed", (qint64) 0);
    check(object, "balance", (qint64) 5000112213);
}
