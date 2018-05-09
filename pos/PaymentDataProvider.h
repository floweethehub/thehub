/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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
#ifndef PAYMENTDATAPROVIDER_H
#define PAYMENTDATAPROVIDER_H

#include "DBConfig.h"
#include "HubConfig.h"
#include "Payment.h"

#include <NetworkManager.h>
#include <streaming/BufferPool.h>
#include <WorkerThreads.h>

#include <QObject>
#include <QPointer>
#include <QSqlDatabase>

class NetworkPaymentProcessor;
class ExchangeRateResolver;

class PaymentDataProvider : public QObject
{
    Q_OBJECT
    Q_ENUMS(ConnectionStatus PaymentStep)
    Q_PROPERTY(QObject* dbConfig READ dbConfig CONSTANT)
    Q_PROPERTY(ConnectionStatus connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(PaymentStep paymentStep READ paymentStep NOTIFY paymentStepChanged)
    Q_PROPERTY(Payment* current READ payment NOTIFY paymentChanged)
public:
    enum ConnectionStatus {
        Connected,
        Disconnected
    };

    enum PaymentStep {
        NoPayment,
        BusyCreatingPayment,
        ShowPayment,
        PartiallyCompletedPayment,
        CompletedPayment
    };

    explicit PaymentDataProvider(QObject *parent = nullptr);

    Q_INVOKABLE void startNewPayment(int amountNative, const QString &comment, const QString &currency = QString());
    Q_INVOKABLE void back(); // go back to text-input
    Q_INVOKABLE void close(); // finish up payment and start new one.

    DBConfig *dbConfig();
    HubConfig *hubConfig();

    ConnectionStatus connected() const {
        return m_connected;
    }
    PaymentStep paymentStep() const {
        return m_paymentStep;
    }

    Payment *payment() const {
        return m_payment;
    }

signals:
    void connectedChanged();
    void paymentStepChanged();
    void paymentChanged();

public slots:
    void connectToDB();
    void exchangeRateUpdated();

private slots:
    void txFound(const QString &bitcoinAddress, const QByteArray &txId, qint64 amount);

private:
    void createTables(const QString &type);
    QString checkCurrency(const QString &hint);
    void updateExchangeRateInDb();

    void onConnected();
    void onDisconnected();
    void onIncomingMessage(const Message &message);

    WorkerThreads m_threads;
    NetworkManager m_manager;
    NetworkConnection m_connection;

    QSqlDatabase m_db;

    DBConfig m_dbConfig;
    HubConfig m_hubConnectionConfig;
    Streaming::BufferPool m_pool;

    ConnectionStatus m_connected = Disconnected;
    PaymentStep m_paymentStep = NoPayment;

    Payment *m_payment = nullptr;

    NetworkPaymentProcessor *m_listener;
    ExchangeRateResolver *m_exchangeRate = nullptr;
};

#endif
