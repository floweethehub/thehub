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
    TestAddressDetails::startRequest(this, m_network);
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
    // case 0:
        // TestAddressDetails::startRequest(this, m_network);
        // break;
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

void TestAddressDetails::startRequest(TestApi *parent, QNetworkAccessManager &manager)
{
    QString blockHeight("%1:%2/v2/address/details/qqdkd86mqx4uxhqk6mcq0n7wt353j6kk9u85lmd68g");
    auto reply = manager.get(QNetworkRequest(blockHeight.arg(parent->hostname()).arg(parent->port())));
    auto o = new TestAddressDetails(reply);
    connect (o, SIGNAL(requestDone()), parent, SLOT(finishedRequest()));
}

void TestAddressDetails::checkDocument(const QJsonDocument &doc)
{
    if (doc.isArray())
        error("Root should not be an array");
    QJsonObject root = doc.object();
    check(root, "balance", 0);
    check(root, "balanceSat", 0);
    check(root, "totalReceived", 49);
    check(root, "totalReceivedSat", (double)4900000000);
    check(root, "totalSent", 49);
    check(root, "totalSentSat", (double)4900000000);
    check(root, "cashAddress", "bitcoincash:qqdkd86mqx4uxhqk6mcq0n7wt353j6kk9u85lmd68g");
    check(root, "legacyAddress", "13VtBWqnSRphhZRvUUir8FVnPZMGPGwi46");

    auto txs_ = root["transactions"];
    if (!txs_.isArray())
        error("no transactions array found");
    QJsonArray txs = txs_.toArray();
    if (txs.size() != 2)
        error("Wrong number of transactions");
    check(txs, 0, "ac771c02c80f4d70f7733a436e06f5de8ecc9e9988e9e5baf727fb479804c99d");
    check(txs, 1, "bec03d0a5384f776e3cd351e37613c0e7924f081081b4352a1fcd69e2f2e8819");
}

