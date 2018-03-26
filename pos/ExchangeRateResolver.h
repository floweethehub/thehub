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
#ifndef EXCHANGERATERESOLVER_H
#define EXCHANGERATERESOLVER_H

#include <QDateTime>
#include <QObject>
#include <QPointer>

class QNetworkReply;
class QNetworkAccessManager;
class Payment;

class ExchangeRateResolver : public QObject
{
    Q_OBJECT
public:
    explicit ExchangeRateResolver(QObject *parent = nullptr);

    void setPrice(int newPrice); // price in cent.
    int price() const {
        return m_price;
    }

    void setExchangeRate(Payment *payment);

signals:
    void priceChanged();

private slots:
    void finishedKrakenFetch();

private:
    bool fetchKraken();

    QNetworkReply *m_reply = nullptr;
    QNetworkAccessManager *m_manager;
    int m_price = -1;
    QDateTime m_lastFetch;

    QList<QPointer<Payment> > m_payments;
};

#endif // EXCHANGERATERESOLVER_H
