/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tom@flowee.org>
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
#include "Payment.h"
#include "PaymentDataProvider.h"
#include "NetworkPaymentProcessor.h"
#include "ExchangeRateResolver.h"

#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>
#include <APIProtocol.h>
#include <utilstrencodings.h>

#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QSqlRecord>
#include <Logger.h>
#include <QGuiApplication>

PaymentDataProvider::PaymentDataProvider(QObject *parent)
    : QObject(parent),
    m_manager(m_threads.ioService()),
    m_listener(nullptr),
    m_exchangeRate(new ExchangeRateResolver(this))
{
    EndPoint ep = HubConfig::readEndPoint(&m_manager);
    m_connection = m_manager.connection(ep);
    if (m_connection.isValid()) {
        m_listener = new NetworkPaymentProcessor(m_manager.connection(ep, NetworkManager::OnlyExisting));
        m_connection.setOnConnected(std::bind(&PaymentDataProvider::onConnected, this));
        m_connection.setOnDisconnected(std::bind(&PaymentDataProvider::onDisconnected, this));
        m_connection.setOnIncomingMessage(std::bind(&PaymentDataProvider::onIncomingMessage, this, std::placeholders::_1));

        connect (m_listener, SIGNAL(txFound(QString,QByteArray,qint64,bool)), this, SLOT(txFound(QString,QByteArray,qint64)));
    }

    connect (m_exchangeRate, SIGNAL(priceChanged()), this, SLOT(exchangeRateUpdated()), Qt::QueuedConnection);
}

void PaymentDataProvider::startNewPayment(int amountNative, const QString &comment, const QString &currency)
{
    logDebug() << "Start payment" << amountNative << comment << currency;
    m_paymentStep = BusyCreatingPayment;
    emit paymentStepChanged();
    if (comment.size() > 100)
        logCritical() << "startNewPayment called with comment longer than 100 chars, will trunkate to 100." << comment;
    QSqlQuery query(m_db);
    if (m_payment == nullptr) {
        query.prepare("insert into PaymentRequests (amountNative, currency, merchantComment) VALUES (:amount, :currency, :comment)");
        query.bindValue(":amount", amountNative);
        query.bindValue(":currency", checkCurrency(currency));
        query.bindValue(":comment", comment.left(100));
        if (!query.exec()) {
            logFatal() << "Failed to insert new payment in database" << query.lastError().text();
            QGuiApplication::exit(1);
            return;
        }
        const int requestId = query.lastInsertId().toInt();
        logCritical() << "inserted new payment. Got requestId:" << requestId;
        m_payment = new Payment(requestId, this);
        m_payment->setOpenTime(QDateTime::currentDateTime());

        Message message(Api::UtilService, Api::Util::CreateAddress);
        message.setHeaderInt(Api::RequestId, requestId);
        m_connection.send(message);
    } else {
        logDebug() << "update payment" << m_payment->requestId();
        query.prepare("update PaymentRequests set (amountNative = :amount, currency = :currency, merchantComment = :comment) where requestId = :id");
        query.bindValue(":amount", amountNative);
        query.bindValue(":currency", checkCurrency(currency));
        query.bindValue(":comment", comment.left(100));
        query.bindValue(":id", m_payment->requestId());
        if (!query.exec()) {
            logFatal() << "Failed to insert new payment in database" << query.lastError().text();
            QGuiApplication::exit(1);
            return;
        }
        m_paymentStep = ShowPayment;
    }
    m_payment->setAmountNative(amountNative);
    m_payment->setMerchantComment(comment);
    m_payment->setNativeCurrency(currency);

    m_exchangeRate->setExchangeRate(m_payment);

    emit paymentChanged();
    emit paymentStepChanged();
}

void PaymentDataProvider::back()
{
    m_paymentStep = NoPayment;
    emit paymentStepChanged();
}

void PaymentDataProvider::close()
{
    if (m_payment) {
        if (!m_payment->closeTime().isValid()) { // abandoned tx, close it now.
            QSqlQuery query(m_db);
            query.prepare("update PaymentRequests set closeTime=CURRENT_TIMESTAMP where requestId=:id");
            query.bindValue(":id", m_payment->requestId());
            if (!query.exec()) {
                logFatal() << "Failed to store closeTime DB" << query.lastError().text();
                QGuiApplication::exit(1);
                return;
            }
        }
        m_payment->deleteLater();
        m_payment = nullptr;
    }

    m_paymentStep = NoPayment;

    emit paymentChanged();
    emit paymentStepChanged();
}

DBConfig *PaymentDataProvider::dbConfig()
{
    return &m_dbConfig;
}

HubConfig *PaymentDataProvider::hubConfig()
{
    return &m_hubConnectionConfig;
}

void PaymentDataProvider::createTables(const QString &type)
{
    QSqlQuery query(m_db);
#if 0
    // show all that is in the main table
    query.exec("select * from PaymentRequests");
    while (query.next()) {
        for (int i = 0; i < 7; ++i)
            logDebug() << query.value(i).toString();
    }
    logDebug() << "----";
    query.exec("select * from Transactions");
    while (query.next()) {
        for (int i = 0; i < 3; ++i)
            logDebug() << query.value(i).toString();
    }
    logDebug() << "----";
    query.exec("select * from Address");
    while (query.next()) {
        for (int i = 0; i < 3; ++i)
            logDebug() << query.value(i).toString();
    }
#endif

    if (!query.exec("select count(*) from PaymentRequests")) {
        QString autoIncrement;
        if (type == "QMYSQL")
            autoIncrement = "AUTO_INCREMENT";
        // else if (type == "QSQLITE") // for sqlite this can stay empty, its implied

        QString q("create table PaymentRequests ( "
                  "requestId INTEGER PRIMARY KEY %1, "
                  "openTime DATETIME DEFAULT CURRENT_TIMESTAMP,"
                  "closeTime DATETIME,"
                  "amountNative int NOT NULL,"
                  "currency varchar(3) NOT NULL,"
                  "exchangeRate int,"
                  "merchantComment varchar(100)"
                  ")");
        q = q.arg(autoIncrement);
        if (!query.exec(q)) {
            logFatal() << "Failed to create table" << query.lastError().text();
            return;
        }
    }
    if (!query.exec("select count(*) from Transactions")) {
        if (!query.exec("create table Transactions ( "
                   "requestId int NOT NULL,"
                   "txid VARBINARY(32) NOT NULL,"
                   "amount long NOT NULL"
                   ")")) {
            logFatal() << "Failed to create index" << query.lastError().text();
        }
        if (!query.exec("create index tx_rq_id on Transactions (requestId)")) {
            logFatal() << "Failed to create index" << query.lastError().text();
        }
    }
    if (!query.exec("select count(*) from Address")) {
        if (!query.exec("create table Address ( "
                   "requestId int NOT NULL,"
                   "bchAddress varchar2(100) NOT NULL,"
                   "bchPrivKey varchar2(60) NOT NULL"
                   ")")) {
            logFatal() << "Failed to create index" << query.lastError().text();
        }
        if (!query.exec("create index ad_rq_id on Address (requestId)")) {
            logFatal() << "Failed to create index" << query.lastError().text();
        }
    }
}

QString PaymentDataProvider::checkCurrency(const QString &hint)
{
    // TODO
    return "EUR";
}

void PaymentDataProvider::updateExchangeRateInDb()
{
    Q_ASSERT(m_payment);
    QSqlQuery query(m_db);
    logDebug() << "update payments exchange rate" << m_payment->requestId() << "to" << m_payment->exchangeRate();
    query.prepare("update PaymentRequests set exchangeRate=:rate where requestId=:id");
    query.bindValue(":rate", m_payment->exchangeRate());
    query.bindValue(":id", m_payment->requestId());
    if (!query.exec()) {
        logFatal() << "Failed to store exchange rate in DB" << query.lastError().text();
        QGuiApplication::exit(1);
        return;
    }
}

void PaymentDataProvider::onConnected()
{
    logDebug() << "connection succeeded";
    m_connected = Connected;
    emit connectedChanged();
}

void PaymentDataProvider::onDisconnected()
{
    logDebug() << "disconnected";
    m_connected = Disconnected;
    emit connectedChanged();
}

void PaymentDataProvider::onIncomingMessage(const Message &message)
{
    if (message.serviceId() == Api::UtilService && message.messageId() == Api::Util::CreateAddressReply)  {
        Streaming::MessageParser parser(message.body());
        QString pub;
        QString priv;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::Util::BitcoinP2PKHAddress) {
                pub = QString::fromUtf8(parser.stringData().data());
            }
            else if (parser.tag() == Api::Util::PrivateKey) {
                priv = QString::fromUtf8(parser.stringData().data());
            }
        }
        const int requestId = message.headerInt(Api::RequestId);
        if (requestId < 1 || pub.isEmpty() || priv.isEmpty()) {
            logCritical() << "Did not get all the fields required from the hub for CreateAddress";
            return;
        }
        logDebug() << "Received a new address" << pub;
        QSqlQuery query(m_db);
        query.prepare("insert into Address (requestId, bchAddress, bchPrivKey) VALUES (:id, :pub, :priv)");
        query.bindValue(":id", requestId);
        query.bindValue(":pub", pub);
        query.bindValue(":priv", priv);
        if (!query.exec()) {
            logFatal() << "Failed to insert address into DB" << query.lastError().text();
            QGuiApplication::exit(1);
            return;
        }
        m_payment->setPubAddress(pub);
        if (m_payment->exchangeRate() > 0) {
            updateExchangeRateInDb();
            m_paymentStep = ShowPayment; //  we already have the exchange rate. Continue.
            emit paymentStepChanged();
        }

        m_listener->addListenAddress(pub);
    }

    else {
        Streaming::MessageParser parser(message.rawData());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.isBool())
                logDebug() << parser.tag() << parser.boolData();
            else if (parser.isLong())
                logDebug() << parser.tag() << parser.longData();
            else if (parser.isInt())
                logDebug() << parser.tag() << parser.isInt();
            else if (parser.isDouble())
                logDebug() << parser.tag() << parser.doubleData();
            else if (parser.isString())
                logDebug() << parser.tag() << parser.stringData();
            else if (parser.isByteArray())
                logDebug() << parser.tag() << HexStr(parser.bytesData());
        }
    }
}

void PaymentDataProvider::connectToDB()
{
    QString type;
    m_db = DBConfig::connectToDB(type);
    if (m_db.isValid() && m_db.open()) {
        createTables(type);
    } else {
        logFatal() << "Failed opening the database-connection" << m_db.lastError().text();
    }
}

void PaymentDataProvider::exchangeRateUpdated()
{
    if (m_paymentStep == BusyCreatingPayment && m_payment && !m_payment->pubAddress().isEmpty()) {
        m_paymentStep = ShowPayment; //  we already have the public address. Continue.
        updateExchangeRateInDb();
        emit paymentStepChanged();
    }
}

void PaymentDataProvider::txFound(const QString &bitcoinAddress, const QByteArray &txId, qint64 amount)
{
    if (m_payment == nullptr)
        return;

    QSqlQuery query(m_db);
    query.prepare("select requestId from Address where bchAddress=:pub");
    query.bindValue(":pub", bitcoinAddress);
    if (!query.exec()) {
        logFatal() << "Failed to select" << query.lastError().text();
        QGuiApplication::exit(1);
        return;
    }
    query.next();
    const int requestId = query.value(0).toInt();
    logDebug() << "got payment for request" << requestId << "amount:" << amount;
    m_payment->addTransaction(txId, amount);

    query.prepare("insert into Transactions (requestId, txid, amount) VALUES (:id, :txid, :amount)");
    query.bindValue(":id", requestId);
    query.bindValue(":txid", txId);
    query.bindValue(":amount", amount);
    if (!query.exec()) {
        logFatal() << "Failed to insert txid" << query.lastError().text();
        QGuiApplication::exit(1);
        return;
    }

    if (m_payment->amountPaid() >= m_payment->amountBch()) {
        m_paymentStep = CompletedPayment;
        QSqlQuery query(m_db);
        query.prepare("update PaymentRequests set closeTime=CURRENT_TIMESTAMP where requestId=:id");
        query.bindValue(":id", m_payment->requestId());
        if (!query.exec()) {
            logFatal() << "Failed to store closeTime DB" << query.lastError().text();
            QGuiApplication::exit(1);
            return;
        }
    }
    else {
        m_paymentStep = PartiallyCompletedPayment;;
    }

    emit paymentStepChanged();
}
