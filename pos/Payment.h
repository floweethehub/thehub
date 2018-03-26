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
#ifndef PAYMENT_H
#define PAYMENT_H

#include <QDateTime>
#include <QObject>
#include <QImage>
#include <QString>

class Payment : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int requestId READ requestId CONSTANT)
    Q_PROPERTY(QDateTime openTime READ openTime NOTIFY openTimeChanged)
    Q_PROPERTY(QDateTime closeTime READ closeTime NOTIFY closeTimeChanged)
    Q_PROPERTY(int amountNative READ amountNative WRITE setAmountNative NOTIFY amountNativeChanged)
    Q_PROPERTY(QString nativeCurrency READ nativeCurrency NOTIFY nativeCurrencyChanged)
    Q_PROPERTY(qint64 amountBch READ amountBch NOTIFY amountBchChanged)
    Q_PROPERTY(QString amountFormatted READ amountFormatted NOTIFY amountBchChanged)
    Q_PROPERTY(QString merchantComment READ merchantComment WRITE setMerchantComment NOTIFY merchantCommentChanged)
    Q_PROPERTY(QString pubAddress READ pubAddress NOTIFY pubAddressChanged)
    Q_PROPERTY(int exchangeRate READ exchangeRate NOTIFY exchangeRateChanged)
    Q_PROPERTY(qint64 amountPaid READ amountPaid NOTIFY amountPaidChanged)
public:
    explicit Payment(QObject *parent = nullptr);

    explicit Payment(int requestId, QObject *parent = nullptr);

    int requestId() const;
    void setRequestId(int requestId);

    QDateTime openTime() const;
    void setOpenTime(const QDateTime &openTime);

    QDateTime closeTime() const;
    void setCloseTime(const QDateTime &closeTime);

    int amountNative() const;
    void setAmountNative(int amountNative);

    QString nativeCurrency() const;
    void setNativeCurrency(const QString &nativeCurrency);

    int exchangeRate() const;
    void setExchangeRate(int exchangeRate);

    qint64 amountBch() const;
    void setAmountBch(const qint64 &amountBch);
    QString amountFormatted() const;

    QString merchantComment() const;
    void setMerchantComment(const QString &merchantComment);

    QString pubAddress() const;
    void setPubAddress(const QString &pubAddress);

    qint64 amountPaid() const;

signals:
    void openTimeChanged();
    void closeTimeChanged();
    void amountNativeChanged();
    void nativeCurrencyChanged();
    void exchangeRateChanged();
    void amountBchChanged();
    void merchantCommentChanged();
    void pubAddressChanged();
    void amountPaidChanged();

public slots:
    void addTransaction(const QByteArray &txid, qint64 amount);

private:
    int m_requestId = -1;
    QDateTime m_openTime, m_closeTime;
    int m_amountNative = -1; // amount in the native currency
    QString m_nativeCurrency; // ISO standard for currencies (EUR, USD etc)
    int m_exchangeRate = -1;
    qint64 m_amountBch = -1; // in satoshis
    QString m_merchantComment;

    struct Transaction {
        QByteArray txid;
        qint64 amount = -1; // in satoshis
    };
    QList<Transaction> m_payments;
    QString m_pubAddress;
};

#endif
