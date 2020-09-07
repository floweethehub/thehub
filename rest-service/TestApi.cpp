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
#include "TestApi.h"

#include <FloweeServiceApplication.h>
#include <QNetworkReply>
#include <QMetaEnum>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QBuffer>

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
    TestAddressDetails::startRequest(this, m_network, TestAddressDetails::GET);
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
        TestAddressDetails::startRequest(this, m_network, TestAddressDetails::POST);
        break;
    case 1:
        TestAddressDetails2::startRequest(this, m_network);
        break;
    case 2:
        TestAddressUTXO::startRequest(this, m_network);
        break;
    case 3:
        TestAddressUTXOPost::startRequest(this, m_network);
        break;
    case 4:
        TestTransactionDetails::startRequest(this, m_network);
        break;
    case 5:
        TestTransactionDetailsPost::startRequest(this, m_network);
        break;
    default:
        QCoreApplication::quit();
    }
}


//////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
    FloweeServiceApplication app(argc, argv);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("rest-service-tester");

    QCommandLineParser parser;
    parser.setApplicationDescription("REST service test-client");
    parser.addHelpOption();
    app.addClientOptions(parser);
    parser.process(app.arguments());

    app.setup();

    logFatal() << "Starting";
    TestApi tester;
    auto server = app.serverAddressFromArguments(3200);
    tester.start(QString::fromStdString(server.hostname), server.announcePort);

    return app.exec();
}


//////////////////////////////////////////////////////////////////

AbstractTestCall::AbstractTestCall(QNetworkReply *parent, CallType callType)
    : QObject(parent),
      m_reply(parent),
      m_callType(callType)
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
    logCritical().nospace() << metaObject()->className() + 4 << " [" << m_reply->url().toString()
                            << ":" << (m_callType == GET ? "GET": "POST") << "]";
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

QByteArray TestAddressDetails::s_postData = {
    "{"
      "\"addresses\": ["
        "\"qqdkd86mqx4uxhqk6mcq0n7wt353j6kk9u85lmd68g\","
        "\"qrehqueqhw629p6e57994436w730t4rzasnly00ht0\""
      "]"
    "}"
};

void TestAddressDetails::startRequest(TestApi *parent, QNetworkAccessManager &manager, CallType type)
{
    QNetworkReply *reply = nullptr;
    QString base("%1:%2/v2/address/details");
    base = base.arg(parent->hostname()).arg(parent->port());

    if (type == GET) {
        reply = manager.get(QNetworkRequest(base + "qqdkd86mqx4uxhqk6mcq0n7wt353j6kk9u85lmd68g"));
    } else {
        QNetworkRequest request(base);
        request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        reply = manager.post(request, s_postData);
    }
    auto o = new TestAddressDetails(reply, type);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestAddressDetails::checkDocument(const QJsonDocument &doc)
{
    QJsonObject answer1, answer2;
    if (m_callType == POST) {
        if (!doc.isArray())
            error("Root should be an array");

        auto array = doc.array();
        if (array.size() != 2)
            error("Root does not have correct number of elements");
        answer1 = array.at(0).toObject();
        answer2 = array.at(1).toObject();
    }
    else {
        if (doc.isArray())
            error("Root should not be an array");
        answer1 = doc.object();
    }
    check(answer1, "balance", 0);
    check(answer1, "balanceSat", 0);
    check(answer1, "totalReceived", 49);
    check(answer1, "totalReceivedSat", (double)4900000000);
    check(answer1, "totalSent", 49);
    check(answer1, "totalSentSat", (double)4900000000);
    check(answer1, "cashAddress", "bitcoincash:qqdkd86mqx4uxhqk6mcq0n7wt353j6kk9u85lmd68g");
    check(answer1, "legacyAddress", "13VtBWqnSRphhZRvUUir8FVnPZMGPGwi46");

    auto txs = checkArray(answer1, "transactions", 2);
    check(txs, 0, "ac771c02c80f4d70f7733a436e06f5de8ecc9e9988e9e5baf727fb479804c99d");
    check(txs, 1, "bec03d0a5384f776e3cd351e37613c0e7924f081081b4352a1fcd69e2f2e8819");

    if (m_callType == POST) {
        check(answer2, "balance", 0);
        check(answer2, "balanceSat", 0);
        check(answer2, "totalReceived", 49);
        check(answer2, "totalReceivedSat", (double)4900000000);
        check(answer2, "totalSent", 49);
        check(answer2, "totalSentSat", (double)4900000000);
        check(answer2, "cashAddress", "bitcoincash:qqdkd86mqx4uxhqk6mcq0n7wt353j6kk9u85lmd68g");
        check(answer2, "legacyAddress", "13VtBWqnSRphhZRvUUir8FVnPZMGPGwi46");

        auto txs2 = checkArray(answer2, "transactions", 2);
        check(txs2, 0, "ac771c02c80f4d70f7733a436e06f5de8ecc9e9988e9e5baf727fb479804c99d");
        check(txs2, 1, "bec03d0a5384f776e3cd351e37613c0e7924f081081b4352a1fcd69e2f2e8819");
    }
}


//////////////////////////////////////////////////////////
void TestAddressDetails2::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString blockHeight("%1:%2/v2/address/details/bitcoincash:qqhtg3y40dgaa7ueprz3mhgkxktpk27sru8t3l2zph");
    auto reply = manager.get(QNetworkRequest(blockHeight.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestAddressDetails2(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestAddressDetails2::checkDocument(const QJsonDocument &doc)
{
    if (doc.isArray())
        error("Root should not be an array");
    QJsonObject root = doc.object();
    check(root, "balance", 39);
    check(root, "balanceSat", (double) 3900000000);
    check(root, "totalReceived", 10044);
    check(root, "totalReceivedSat", (double) 1004400000000);
    check(root, "totalSent", 10005);
    check(root, "totalSentSat", (double) 1000500000000);
    check(root, "cashAddress", "bitcoincash:qqhtg3y40dgaa7ueprz3mhgkxktpk27sru8t3l2zph");
    check(root, "legacyAddress", "15Fx34MisMrqThpkmFdC6U2uGW6SRKVwh4");

    auto txs = checkArray(root, "transactions", 240);
    check(txs, 0, "221fd0f3b12d6d76027f21753fd64c644dbbf34405333ca1565a6a75d937c8ac");
    check(txs, 1, "f2d5540968fe76c7d4ae0f183e34e873ea4deea492fde56cff5b2cc7920942f0");
    check(txs, 2, "70a00f731e6b3bf959834f429a548546487d3f71d247cc78a12a78df9d1eb7de");
    check(txs, 3, "a0d643d1f64fadb3d4039fe8b78d5d1ff8f16705613aa7918551abde57315af7");
    check(txs, 4, "19c86fcdcb5f7c572f4d5d1176a2e00004ac9311f219ce469136f65fcf2985cf");
    check(txs, 7, "db6d13b57fb0daef6ebb8af735a4b2776f11143e760d0c90e4251613bb00e43b");
    check(txs, 225, "cbc4418fb87fde759fb02435b620774c0eccd1b238eab15a4c839a77a7c0cc0e");
}


//////////////////////////////////////////////////////////

void TestAddressUTXO::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString blockHeight("%1:%2/v2/address/utxo/qqhtg3y40dgaa7ueprz3mhgkxktpk27sru8t3l2zph");
    auto reply = manager.get(QNetworkRequest(blockHeight.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestAddressUTXO(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestAddressUTXO::checkDocument(const QJsonDocument &doc)
{
    if (doc.isArray())
        error("Root should not be an array");
    QJsonObject root = doc.object();
    check(root, "cashAddress", "bitcoincash:qqhtg3y40dgaa7ueprz3mhgkxktpk27sru8t3l2zph");
    check(root, "legacyAddress", "15Fx34MisMrqThpkmFdC6U2uGW6SRKVwh4");
    // TODO slpAddress ??
    // scriptPubKey
    // asm

    auto utxos = checkArray(root, "utxos", 1);
    auto tx0 = utxos[0];
    check(tx0, "vout", 0);
    check(tx0, "amount", 39);
    check(tx0, "satoshis", (double) 3900000000);
    check(tx0, "height", 178290);
    check(tx0, "txid", "221fd0f3b12d6d76027f21753fd64c644dbbf34405333ca1565a6a75d937c8ac");
    // optional: confirmations
}


//////////////////////////////////////////////////////////

QByteArray TestAddressUTXOPost::s_postData = {
    "{"
      "\"addresses\": ["
        "\"qqhtg3y40dgaa7ueprz3mhgkxktpk27sru8t3l2zph\","
        "\"qrehqueqhw629p6e57994436w730t4rzasnly00ht0\""
      "]"
    "}"
};

void TestAddressUTXOPost::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString base("%1:%2/v2/address/utxo");
    QNetworkRequest request(base.arg(parent->hostname()).arg(parent->port()));
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    auto reply = manager.post(request, s_postData);
    auto o = new TestAddressUTXOPost(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestAddressUTXOPost::checkDocument(const QJsonDocument &doc)
{
    if (!doc.isArray())
        error("Root should be an array");
    QJsonArray array = doc.array();
    if (array.size() != 2)
        error("Incorrect number of root elements");
    if (!array.at(0).isObject())
        error("Array(0) should be an object");
    QJsonObject root = array.at(0).toObject();
    check(root, "cashAddress", "bitcoincash:qqhtg3y40dgaa7ueprz3mhgkxktpk27sru8t3l2zph");
    check(root, "legacyAddress", "15Fx34MisMrqThpkmFdC6U2uGW6SRKVwh4");
    // TODO slpAddress ??
    // scriptPubKey
    // asm

    auto utxos = checkArray(root, "utxos", 1);
    auto tx0 = utxos[0];
    check(tx0, "vout", 0);
    check(tx0, "amount", 39);
    check(tx0, "satoshis", (double) 3900000000);
    check(tx0, "height", 178290);
    check(tx0, "txid", "221fd0f3b12d6d76027f21753fd64c644dbbf34405333ca1565a6a75d937c8ac");
    // optional: confirmations

    // TODO the second answer
    root = array.at(1).toObject();
    // TODO the answers
    check(root, "cashAddress", "bitcoincash:qrehqueqhw629p6e57994436w730t4rzasnly00ht0");
    check(root, "legacyAddress", "1PCBukyYULnmraUpMy2hW1Y1ngEQTN8DtF");
    // TODO slpAddress ??
    // scriptPubKey
    // asm

    utxos = checkArray(root, "utxos", 2);
    tx0 = utxos[0];
    check(tx0, "vout", 0);
    check(tx0, "amount", 0.00051061);
    check(tx0, "satoshis", (double) 51061);
    check(tx0, "height", 560615);
    check(tx0, "txid", "b3792d28377b975560e1b6f09e48aeff8438d4c6969ca578bd406393bd50bd7d");
    auto tx1 = utxos[1];
    check(tx1, "vout", 1);
    check(tx1, "amount", 0.00531373);
    check(tx1, "satoshis", (double) 531373);
    check(tx1, "height", 562106);
    check(tx1, "txid", "1afcc63b244182647909539ebe3f4a44b8ea4120a95edb8d9eebe5347b9491bb");
}


//////////////////////////////////////////////////////////

void TestTransactionDetails::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString blockHeight("%1:%2/v2/transaction/details/221fd0f3b12d6d76027f21753fd64c644dbbf34405333ca1565a6a75d937c8ac");
    auto reply = manager.get(QNetworkRequest(blockHeight.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestTransactionDetails(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestTransactionDetails::checkDocument(const QJsonDocument &doc)
{
    if (doc.isArray())
        error("Root should not be an array");
    checkDetails221fd0f3(doc.object());
}

void TestTransactionDetails::checkDetails221fd0f3(const QJsonObject &root)
{
    check(root, "txid", "221fd0f3b12d6d76027f21753fd64c644dbbf34405333ca1565a6a75d937c8ac");
    check(root, "version", 1);
    check(root, "locktime", 0);
    check(root, "blockhash", "000000000000073e9769b8839e8b28f1d6a82eee6e3c94b3e866332bc0f86d13");
    check(root, "blockheight", 178290);
    check(root, "time", 1335978635);
    check(root, "blocktime", 1335978635);
    check(root, "firstSeenTime", QJsonValue::Null);
    check(root, "size", 224);
    check(root, "valueOut", 39);
    check(root, "valueIn", 39);
    check(root, "fees", QJsonValue::Null); // WFT? zero would be more appropriate
    // optional: confirmations

    auto inputs = checkArray(root, "vin", 1);
    auto in1 = inputs[0];
    check (in1, "txid", "d0519ef40c6704ccd8f55f0e14627f7d716d58df796ea4980875ab266daba6be");
    check (in1, "vout", 1);
    check (in1, "n", 0);
    check (in1, "value", (double)3900000000);
    check (in1, "legacyAddress", "19rRh2VahedZdLxPhsJLjJWCwwEqRoS4PU");
    check (in1, "cashAddress", "bitcoincash:qps3nla86vdczawucy28ha5reay2ghmwdc66x8xd85");
    auto scriptSig = checkProp(in1, "scriptSig");
    check (scriptSig, "hex", "4830450220588378deeafd55e05a2d5cc07fc7010990b"
        "0738b0da32882e482e95df5c3b68a022100a36419800033620a7369423047a96cd"
        "1e6537b54eb86f4f12a4d3c14819edad301410429042110774d8f75f01dceb2881"
        "995ab34c46743f33859142991498adf93a27010446ab98b910a3924c3ea96a8d8b"
        "1accf05a3fa54ebc2953ebf39f1d57890fd");
    check (scriptSig, "asm", "30450220588378deeafd55e05a2d5cc07fc7010990b07"
        "38b0da32882e482e95df5c3b68a022100a36419800033620a7369423047a96cd1e"
        "6537b54eb86f4f12a4d3c14819edad301 "
        "0429042110774d8f75f01dceb2881995ab34c46743f33859142991498adf93a270"
        "10446ab98b910a3924c3ea96a8d8b1accf05a3fa54ebc2953ebf39f1d57890fd");

    auto outputs = checkArray(root, "vout", 1);
    auto out1 = outputs[0];
    check(out1, "value", "39.00000000");
    check(out1, "n", 0);
    check(out1, "spentTxId", QJsonValue::Null);
    check(out1, "spentIndex", QJsonValue::Null);
    check(out1, "spentHeight", QJsonValue::Null);

    auto scriptPubKey = checkProp(out1, "scriptPubKey");
    check (scriptPubKey, "hex", "76a9142eb444957b51defb9908c51ddd1635961b2bd01f88ac");
    check (scriptPubKey, "asm", "OP_DUP OP_HASH160 2eb444957b51defb9908c51ddd16"
        "35961b2bd01f OP_EQUALVERIFY OP_CHECKSIG");
    check (scriptPubKey, "type", "pubkeyhash");
    auto ad1 = checkArray(scriptPubKey, "addresses", 1);
    check(ad1, 0, "15Fx34MisMrqThpkmFdC6U2uGW6SRKVwh4");
    auto ad2 = checkArray(scriptPubKey, "cashAddrs", 1);
    check(ad2, 0, "bitcoincash:qqhtg3y40dgaa7ueprz3mhgkxktpk27sru8t3l2zph");
}


//////////////////////////////////////////////////////////


QByteArray TestTransactionDetailsPost::s_postData = {
    "{"
      "\"txs\": ["
        "\"221fd0f3b12d6d76027f21753fd64c644dbbf34405333ca1565a6a75d937c8ac\","
        "\"1afcc63b244182647909539ebe3f4a44b8ea4120a95edb8d9eebe5347b9491bb\""
      "]"
    "}"
};

void TestTransactionDetailsPost::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString base("%1:%2/v2/transaction/details");
    QNetworkRequest request(base.arg(parent->hostname()).arg(parent->port()));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto reply = manager.post(request, s_postData);
    auto o = new TestTransactionDetailsPost(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestTransactionDetailsPost::checkDocument(const QJsonDocument &doc)
{
    if (!doc.isArray())
        error("Root should be an array");
    auto array = doc.array();
    if (array.size() != 2)
        error("Expected 2 items on the array");
    auto tx1 = array.at(0);
    if (!tx1.isObject())
        error("Item 0 should be an object {}");
    checkDetails221fd0f3(tx1.toObject());

    auto tx2_ = array.at(1);
    if (!tx2_.isObject())
        error("Item 1 should be an object {}");
    auto tx2 = tx2_.toObject();
    check(tx2, "txid", "221fd0f3b12d6d76027f21753fd64c644dbbf34405333ca1565a6a75d937c8ac");
    check(tx2, "version", 1);
    check(tx2, "locktime", 0);
    check(tx2, "blockhash", "000000000000073e9769b8839e8b28f1d6a82eee6e3c94b3e866332bc0f86d13");
    check(tx2, "blockheight", 178290);
    check(tx2, "time", 1335978635);
    check(tx2, "blocktime", 1335978635);
    check(tx2, "firstSeenTime", QJsonValue::Null);
    check(tx2, "size", 224);
    check(tx2, "valueOut", 39);
    check(tx2, "valueIn", 39);
    check(tx2, "fees", QJsonValue::Null); // WFT? zero would be more appropriate
    // optional: confirmations

    auto inputs = checkArray(tx2, "vin", 1);
    auto in1 = inputs[0];
    check (in1, "txid", "d0519ef40c6704ccd8f55f0e14627f7d716d58df796ea4980875ab266daba6be");
    check (in1, "vout", 1);
    check (in1, "n", 0);
    check (in1, "value", (double)3900000000);
    check (in1, "legacyAddress", "19rRh2VahedZdLxPhsJLjJWCwwEqRoS4PU");
    check (in1, "cashAddress", "bitcoincash:qps3nla86vdczawucy28ha5reay2ghmwdc66x8xd85");
    auto scriptSig = checkProp(in1, "scriptSig");
    check (scriptSig, "hex", "4830450220588378deeafd55e05a2d5cc07fc7010990b"
        "0738b0da32882e482e95df5c3b68a022100a36419800033620a7369423047a96cd"
        "1e6537b54eb86f4f12a4d3c14819edad301410429042110774d8f75f01dceb2881"
        "995ab34c46743f33859142991498adf93a27010446ab98b910a3924c3ea96a8d8b"
        "1accf05a3fa54ebc2953ebf39f1d57890fd");
    check (scriptSig, "asm", "30450220588378deeafd55e05a2d5cc07fc7010990b07"
        "38b0da32882e482e95df5c3b68a022100a36419800033620a7369423047a96cd1e"
        "6537b54eb86f4f12a4d3c14819edad301 "
        "0429042110774d8f75f01dceb2881995ab34c46743f33859142991498adf93a270"
        "10446ab98b910a3924c3ea96a8d8b1accf05a3fa54ebc2953ebf39f1d57890fd");

    auto outputs = checkArray(tx2, "vout", 1);
    auto out1 = outputs[0];
    check(out1, "value", "39.00000000");
    check(out1, "n", 0);
    check(out1, "spentTxId", QJsonValue::Null);
    check(out1, "spentIndex", QJsonValue::Null);
    check(out1, "spentHeight", QJsonValue::Null);

    auto scriptPubKey = checkProp(out1, "scriptPubKey");
    check (scriptPubKey, "hex", "76a9142eb444957b51defb9908c51ddd1635961b2bd01f88ac");
    check (scriptPubKey, "asm", "OP_DUP OP_HASH160 2eb444957b51defb9908c51ddd16"
        "35961b2bd01f OP_EQUALVERIFY OP_CHECKSIG");
    check (scriptPubKey, "type", "pubkeyhash");
    auto ad1 = checkArray(scriptPubKey, "addresses", 1);
    check(ad1, 0, "15Fx34MisMrqThpkmFdC6U2uGW6SRKVwh4");
    auto ad2 = checkArray(scriptPubKey, "cashAddrs", 1);
    check(ad2, 0, "bitcoincash:qqhtg3y40dgaa7ueprz3mhgkxktpk27sru8t3l2zph");
}
