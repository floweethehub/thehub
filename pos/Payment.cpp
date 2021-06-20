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

Payment::Payment(QObject *parent) : QObject(parent)
{
}

Payment::Payment(int requestId, QObject *parent)
    : QObject(parent),
      m_requestId(requestId)
{
}

int Payment::requestId() const
{
    return m_requestId;
}

void Payment::setRequestId(int requestId)
{
    m_requestId = requestId;
}

QDateTime Payment::openTime() const
{
    return m_openTime;
}

void Payment::setOpenTime(const QDateTime &openTime)
{
    if (m_openTime == openTime)
        return;
    m_openTime = openTime;
    emit openTimeChanged();
}

QDateTime Payment::closeTime() const
{
    return m_closeTime;
}

void Payment::setCloseTime(const QDateTime &closeTime)
{
    if (m_closeTime == closeTime)
        return;
    m_closeTime = closeTime;
    emit closeTimeChanged();
}

int Payment::amountNative() const
{
    return m_amountNative;
}

void Payment::setAmountNative(int amountNative)
{
    if (amountNative == m_amountNative)
        return;
    m_amountNative = amountNative;
    m_amountBch = (quint64) m_amountNative * 100000000 / m_exchangeRate;
    emit amountNativeChanged();
    emit amountBchChanged();
}

QString Payment::nativeCurrency() const
{
    return m_nativeCurrency;
}

void Payment::setNativeCurrency(const QString &nativeCurrency)
{
    if (nativeCurrency == m_nativeCurrency)
        return;
    m_nativeCurrency = nativeCurrency;
    emit nativeCurrencyChanged();
}

int Payment::exchangeRate() const
{
    return m_exchangeRate;
}

void Payment::setExchangeRate(int exchangeRate)
{
    if (m_exchangeRate == exchangeRate)
        return;
    m_exchangeRate = exchangeRate;
    m_amountBch = (quint64) m_amountNative * 100000000 / m_exchangeRate;
    emit exchangeRateChanged();
    emit amountBchChanged();
}

qint64 Payment::amountBch() const
{
    return m_amountBch;
}

void Payment::setAmountBch(const qint64 &amountBch)
{
    if (amountBch == m_amountBch)
        return;
    m_amountBch = amountBch;
    emit amountBchChanged();
}

QString Payment::amountFormatted() const
{
    return QString::number(m_amountBch / 1E8, 'f', 8);
}

QString Payment::merchantComment() const
{
    return m_merchantComment;
}

void Payment::setMerchantComment(const QString &merchantComment)
{
    if (m_merchantComment == merchantComment)
        return;
    m_merchantComment = merchantComment;
    emit merchantCommentChanged();
}

QString Payment::pubAddress() const
{
    return m_pubAddress;
}

void Payment::setPubAddress(const QString &pubAddress)
{
    if (pubAddress == m_pubAddress)
        return;
    m_pubAddress = pubAddress;
    emit pubAddressChanged();
}

qint64 Payment::amountPaid() const
{
    qint64 paid = 0;
    for (auto tx : m_payments) {
        paid += tx.amount;
    }
    return paid;
}

void Payment::addTransaction(const QByteArray &txid, qint64 amount)
{
    Transaction tx;
    tx.amount = amount;
    tx.txid = txid;
    m_payments.append(tx);
    emit amountPaidChanged();
}
