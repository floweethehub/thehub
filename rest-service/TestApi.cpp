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
    case 6:
        GetRawTransactionVerbose::startRequest(this, m_network);
        break;
    case 7:
        GetRawTransaction::startRequest(this, m_network);
        break;
    case 8:
        SendRawTransaction::startRequest(this, m_network);
        break;
    case 9:
        SendRawTransaction::startRequest(this, m_network, SendRawTransaction::POST);
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
                            << " :" << (m_callType == GET ? "GET": "POST") << "]";
    for (auto x : m_reply->rawHeaderList()) {
        logInfo().nospace() << "  " << QString::fromLatin1(x) << ": " << QString::fromUtf8(m_reply->rawHeader(x));
    }
    if (m_reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(m_reply->readAll());
        if (doc.isNull()) {
            error("  document could not be parsed, is it JSON?");
        } else try {
            checkDocument(doc);
        } catch (...) {
            if (m_errors.isEmpty())
                m_errors.append(Error{"Runner", "Crashed"});
        }

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
        reply = manager.get(QNetworkRequest(base + "/qqdkd86mqx4uxhqk6mcq0n7wt353j6kk9u85lmd68g"));
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
        if (!doc.isArray()) {
            error("Root should be an array");
            return;
        }

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
        check(answer2, "balance", 0.00582434);
        check(answer2, "balanceSat", (double) 582434);
        check(answer2, "totalReceived", 2.13667684);
        check(answer2, "totalReceivedSat", (double) 213667684);
        check(answer2, "totalSent", 2.1308525);
        check(answer2, "totalSentSat", (double) 213085250);
        check(answer2, "cashAddress", "bitcoincash:qrehqueqhw629p6e57994436w730t4rzasnly00ht0");
        check(answer2, "legacyAddress", "1PCBukyYULnmraUpMy2hW1Y1ngEQTN8DtF");

        auto txs2 = checkArray(answer2, "transactions", 14);
        check(txs2, 0, "0037c0460178a223ca2b90a987244908fd38d471dcae76a60754b170f7c29b93");
        check(txs2, 1, "dbc04814c34a66185e3aa53b246bb7ddacc03d74d4801834434efd513e55c203");
        check(txs2, 6, "ceb0cab0e37b59caf3ca29e1a698d19ff47f2827dd09cb2f3b91b9100b1dad1c");
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
    check(root, "balance", (double) 39.00000547);
    check(root, "balanceSat", (double) 3900000547);
    check(root, "totalReceived", 10044.00000547);
    check(root, "totalReceivedSat", (double) 1004400000547);
    check(root, "totalSent", 10005);
    check(root, "totalSentSat", (double) 1000500000000);
    check(root, "cashAddress", "bitcoincash:qqhtg3y40dgaa7ueprz3mhgkxktpk27sru8t3l2zph");
    check(root, "legacyAddress", "15Fx34MisMrqThpkmFdC6U2uGW6SRKVwh4");

    auto txs = checkArray(root, "transactions", 241);
    check(txs, 1, "221fd0f3b12d6d76027f21753fd64c644dbbf34405333ca1565a6a75d937c8ac");
    check(txs, 2, "f2d5540968fe76c7d4ae0f183e34e873ea4deea492fde56cff5b2cc7920942f0");
    check(txs, 3, "70a00f731e6b3bf959834f429a548546487d3f71d247cc78a12a78df9d1eb7de");
    check(txs, 4, "a0d643d1f64fadb3d4039fe8b78d5d1ff8f16705613aa7918551abde57315af7");
    check(txs, 5, "19c86fcdcb5f7c572f4d5d1176a2e00004ac9311f219ce469136f65fcf2985cf");
    check(txs, 8, "db6d13b57fb0daef6ebb8af735a4b2776f11143e760d0c90e4251613bb00e43b");
    check(txs, 226, "cbc4418fb87fde759fb02435b620774c0eccd1b238eab15a4c839a77a7c0cc0e");
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

    auto utxos = checkArray(root, "utxos", 2);
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
    if (!doc.isArray()) {
        error("Root should be an array");
        return;
    }
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

    auto utxos = checkArray(root, "utxos", 2);
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
    if (utxos.isEmpty())
        return;
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
    checkDetails221fd0f3_more(doc.object());
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
    check(root, "size", 224);

    auto inputs = checkArray(root, "vin", 1);
    auto in1 = inputs[0];
    check (in1, "txid", "d0519ef40c6704ccd8f55f0e14627f7d716d58df796ea4980875ab266daba6be");
    check (in1, "vout", 1);
    check (in1, "n", 0);
    auto scriptSig = checkProp(in1, "scriptSig");
    check (scriptSig, "hex", "4830450220588378deeafd55e05a2d5cc07fc7010990b"
        "0738b0da32882e482e95df5c3b68a022100a36419800033620a7369423047a96cd"
        "1e6537b54eb86f4f12a4d3c14819edad301410429042110774d8f75f01dceb2881"
        "995ab34c46743f33859142991498adf93a27010446ab98b910a3924c3ea96a8d8b"
        "1accf05a3fa54ebc2953ebf39f1d57890fd");
//   check (scriptSig, "asm", "30450220588378deeafd55e05a2d5cc07fc7010990b07"
//       "38b0da32882e482e95df5c3b68a022100a36419800033620a7369423047a96cd1e"
//       "6537b54eb86f4f12a4d3c14819edad301 "
//       "0429042110774d8f75f01dceb2881995ab34c46743f33859142991498adf93a270"
//       "10446ab98b910a3924c3ea96a8d8b1accf05a3fa54ebc2953ebf39f1d57890fd");

    auto outputs = checkArray(root, "vout", 1);
    auto out1 = outputs[0];
    check(out1, "n", 0);

    auto scriptPubKey = checkProp(out1, "scriptPubKey");
    check (scriptPubKey, "hex", "76a9142eb444957b51defb9908c51ddd1635961b2bd01f88ac");
//   check (scriptPubKey, "asm", "OP_DUP OP_HASH160 2eb444957b51defb9908c51ddd16"
//       "35961b2bd01f OP_EQUALVERIFY OP_CHECKSIG");
    check (scriptPubKey, "type", "pubkeyhash");
}

void TestTransactionDetails::checkDetails221fd0f3_more(const QJsonObject &root)
{
    check(root, "firstSeenTime", QJsonValue::Null);
    check(root, "valueOut", 39);
    check(root, "valueIn", 39);
    check(root, "fees", QJsonValue::Null); // WFT? zero would be more appropriate
    // optional: confirmations

    auto inputs = checkArray(root, "vin", 1);
    auto in1 = inputs[0];
    check (in1, "value", (double)3900000000);
    check (in1, "legacyAddress", "19rRh2VahedZdLxPhsJLjJWCwwEqRoS4PU");
    check (in1, "cashAddress", "bitcoincash:qps3nla86vdczawucy28ha5reay2ghmwdc66x8xd85");

    auto outputs = checkArray(root, "vout", 1);
    auto out1 = outputs[0];
    check(out1, "value", "39.00000000");
    check(out1, "spentTxId", QJsonValue::Null);
    check(out1, "spentIndex", QJsonValue::Null);
    check(out1, "spentHeight", QJsonValue::Null);

    auto scriptPubKey = checkProp(out1, "scriptPubKey");
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
    checkDetails221fd0f3_more(tx1.toObject());

    auto tx2_ = array.at(1);
    if (!tx2_.isObject())
        error("Item 1 should be an object {}");
    auto tx2 = tx2_.toObject();
    check(tx2, "txid", "1afcc63b244182647909539ebe3f4a44b8ea4120a95edb8d9eebe5347b9491bb");
    check(tx2, "version", 1);
    check(tx2, "locktime", 0);
    check(tx2, "blockhash", "0000000000000000045e5e52fb4f9746b3d15d3062855fd346aaef3debef4360");
    check(tx2, "blockheight", 562106);
    check(tx2, "time", 1545564654);
    check(tx2, "blocktime", 1545564654);
    check(tx2, "firstSeenTime", QJsonValue::Null);
    check(tx2, "size", 437);
    check(tx2, "valueOut", 0.47531373);
    check(tx2, "valueIn", 0.47541373);
    check(tx2, "fees", 0.0001);
    // optional: confirmations

    auto inputs = checkArray(tx2, "vin", 2);
    auto in1 = inputs[0];
    check (in1, "txid", "c42f8f16d3baa2ee343ea89ef110dfe094992379d08edd30887b8ca7ee671c9a");
    check (in1, "vout", 0);
    check (in1, "n", 0);
    check (in1, "value", 25572607.);
    check (in1, "legacyAddress", "1PCBukyYULnmraUpMy2hW1Y1ngEQTN8DtF");
    check (in1, "cashAddress", "bitcoincash:qrehqueqhw629p6e57994436w730t4rzasnly00ht0");
    auto scriptSig = checkProp(in1, "scriptSig");
    check (scriptSig, "hex", "4830450221008052d3b067418d53585fb8f91e1b57cf3"
        "c040dc9c07a70f393ed663b3f7502c50220749aa8e09ac922e78cb474c8097873c"
        "fb2634108d7acaa7db32a73a35743da974141044eb40b025df18409f2a5197b010"
        "dd62a9e65d9a74e415e5b10367721a9c4baa7ebfee22d14b8ece1c9bd70c0d9e5e"
        "8b00b61b81b88a1b5ce6f24eac6b8a34b2c");
    auto in2 = inputs[1];
    check (in2, "txid", "e4a0ac48ff3f42fc342717a2a3d34248e5e85bae79d59bd20e1b60e61b1c500f");
    check (in2, "vout", 1);
    check (in2, "n", 1);
    check (in2, "value", 21968766.);
    check (in2, "legacyAddress", "1PCBukyYULnmraUpMy2hW1Y1ngEQTN8DtF");
    check (in2, "cashAddress", "bitcoincash:qrehqueqhw629p6e57994436w730t4rzasnly00ht0");
    scriptSig = checkProp(in2, "scriptSig");
    check (scriptSig, "hex", "473044022050d7fe7cdcec81eefa0987b88ddb83274d8e"
        "9063d927090dc4c2d1db76c512d302207dc1eea439a627476265ed87f59cc9823fb"
        "572ffc2640f0218d7bddc9a621c6e4141044eb40b025df18409f2a5197b010dd62a"
        "9e65d9a74e415e5b10367721a9c4baa7ebfee22d14b8ece1c9bd70c0d9e5e8b00b6"
        "1b81b88a1b5ce6f24eac6b8a34b2c");

    auto outputs = checkArray(tx2, "vout", 2);
    auto out1 = outputs[0];
    check(out1, "value", "0.47000000");
    check(out1, "n", 0);
    check(out1, "spentTxId", "5994ec5d40d5c77d4cebd6988de5c4b58961539f3aca8f079ca39d923100adf6");
    check(out1, "spentIndex", 0);
    check(out1, "spentHeight", 626385);

    auto scriptPubKey = checkProp(out1, "scriptPubKey");
    check (scriptPubKey, "hex", "76a9147ab928d0b41194411a2e87a782b688c7cc69ba4688ac");
    check (scriptPubKey, "type", "pubkeyhash");
    auto ad1 = checkArray(scriptPubKey, "addresses", 1);
    check(ad1, 0, "1CBuFWNQsRAy25xGsBoXTxNeRpd5t8be1a");
    auto ad2 = checkArray(scriptPubKey, "cashAddrs", 1);
    check(ad2, 0, "bitcoincash:qpatj2xsksgegsg696r60q4k3rruc6d6gc3srp333v");


    auto out2 = outputs[1];
    check(out2, "value", "0.00531373");
    check(out2, "n", 1);
    check(out2, "spentTxId", QJsonValue::Null);
    check(out2, "spentIndex", QJsonValue::Null);
    check(out2, "spentHeight", QJsonValue::Null);

    scriptPubKey = checkProp(out2, "scriptPubKey");
    check (scriptPubKey, "hex", "76a914f3707320bbb4a28759a78a5ad63a77a2f5d462ec88ac");
    check (scriptPubKey, "type", "pubkeyhash");
    ad1 = checkArray(scriptPubKey, "addresses", 1);
    check(ad1, 0, "1PCBukyYULnmraUpMy2hW1Y1ngEQTN8DtF");
    ad2 = checkArray(scriptPubKey, "cashAddrs", 1);
    check(ad2, 0, "bitcoincash:qrehqueqhw629p6e57994436w730t4rzasnly00ht0");
}

//////////////////////////////////////////////////////////

void GetRawTransactionVerbose::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString request("%1:%2/v2/rawtransactions/getRawTransaction/221fd0f3b12d6d76027f21753fd64c644dbbf34405333ca1565a6a75d937c8ac?verbose=true");
    auto reply = manager.get(QNetworkRequest(request.arg(parent->hostname()).arg(parent->port())));
    auto o = new GetRawTransactionVerbose(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void GetRawTransactionVerbose::checkDocument(const QJsonDocument &doc)
{
    if (doc.isArray())
        error("Root should not be an array");
    auto root = doc.object();
    checkDetails221fd0f3(root);

    auto outputs = checkArray(root, "vout", 1);
    auto out1 = outputs[0];
    check(out1, "value", 39);
    auto scriptPubKey = checkProp(out1, "scriptPubKey");
    auto ad = checkArray(scriptPubKey, "addresses", 1);
    check(ad, 0, "bitcoincash:qqhtg3y40dgaa7ueprz3mhgkxktpk27sru8t3l2zph");
}


//////////////////////////////////////////////////////////

void GetRawTransaction::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString request("%1:%2/v2/rawtransactions/getRawTransaction/221fd0f3b12d6d76027f21753fd64c644dbbf34405333ca1565a6a75d937c8ac");
    auto reply = manager.get(QNetworkRequest(request.arg(parent->hostname()).arg(parent->port())));
    auto o = new GetRawTransaction(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

GetRawTransaction::GetRawTransaction(QNetworkReply *parent)
    : m_reply(parent)
{
    connect (m_reply, SIGNAL(finished()), this, SLOT(finished()));
    QTimer::singleShot(10000, this, SLOT(timeout()));
}

void GetRawTransaction::finished()
{
    QByteArray data = m_reply->readAll();
    QString out = QString::fromLatin1(data);

    if (out != "0100000001bea6ab6d26ab750898a46e79df586d717d7f621"
            "40e5ff5d8cc04670cf49e51d0010000008b4830450220588378d"
            "eeafd55e05a2d5cc07fc7010990b0738b0da32882e482e95df5c"
            "3b68a022100a36419800033620a7369423047a96cd1e6537b54e"
            "b86f4f12a4d3c14819edad301410429042110774d8f75f01dceb"
            "2881995ab34c46743f33859142991498adf93a27010446ab98b9"
            "10a3924c3ea96a8d8b1accf05a3fa54ebc2953ebf39f1d57890f"
            "dffffffff01004775e8000000001976a9142eb444957b51defb9"
            "908c51ddd1635961b2bd01f88ac00000000") {

        logFatal() << "  ❎" << "GetRawTransaction got the wrong hash back";
    }

    deleteLater();
    emit requestDone();
}

void GetRawTransaction::timeout()
{
    logCritical() << m_reply->url().toString();
    logCritical() << "  ❎ Request never returned";
    deleteLater();
    emit requestDone();
}


//////////////////////////////////////////////////////////

static const QByteArray s_txToSend("01000000013ba3edfd7a7b12b27ac72c3e67768"
     "f617fc81bc3888a51323a9fb8aa4b1e5e4a000000006a4730440220540986d1c58d6e76"
     "f8f05501c520c38ce55393d0ed7ed3c3a82c69af04221232022058ea43ed6c05fec0ecc"
     "ce749a63332ed4525460105346f11108b9c26df93cd72012103083dfc5a0254613941dd"
     "c91af39ff90cd711cdcde03a87b144b883b524660c39ffffffff01807c814a000000001"
     "976a914d7e7c4e0b70eaa67ceff9d2823d1bbb9f6df9a5188ac00000000");

QByteArray SendRawTransaction::s_postData("{"
       "\"hexes\": [\"" + s_txToSend + "\"]}");

void SendRawTransaction::startRequest(TestApi *parent, QNetworkAccessManager &manager, CallType type)
{
    QNetworkReply *reply = nullptr;
    QString base("%1:%2/v2/rawtransactions/sendRawTransaction");
    base = base.arg(parent->hostname()).arg(parent->port());
    if (type == GET) {
        reply = manager.get(QNetworkRequest(base + "/" + QString::fromLatin1(s_txToSend)));
    }
    else {
        QNetworkRequest request(base);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = manager.post(request, s_postData);
    }
    auto o = new SendRawTransaction(reply, type);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void SendRawTransaction::checkDocument(const QJsonDocument &doc)
{
    if (doc.isArray())
        error("Root should not be an array");
    auto root = doc.object();
    check(root, "error", "Missing inputs");
}
