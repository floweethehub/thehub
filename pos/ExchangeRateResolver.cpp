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
#include "ExchangeRateResolver.h"
#include "Payment.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

ExchangeRateResolver::ExchangeRateResolver(QObject *parent)
    : QObject(parent),
    m_manager(new QNetworkAccessManager(this))
{
#ifndef NDEBUG
    setPrice(80000);
#endif
}

void ExchangeRateResolver::setPrice(int newPrice)
{
    if (newPrice == m_price)
        return;
    m_lastFetch = QDateTime::currentDateTimeUtc();
    m_price = newPrice;
    emit priceChanged();
}

void ExchangeRateResolver::setExchangeRate(Payment *payment)
{
    if (fetchKraken()) {
        QPointer<Payment> p(payment);
        m_payments.append(p);
    } else { // from cache
        payment->setExchangeRate(m_price);
    }
}

void ExchangeRateResolver::finishedKrakenFetch()
{
    Q_ASSERT(m_reply);
    QByteArray input = m_reply->readAll();
    m_reply->deleteLater();

    QJsonObject root = QJsonDocument::fromJson(input).object();
    //auto error = root["error"]; // TODO
    auto resultRef = root.value(QLatin1String("result"));
    if (resultRef.isObject()) {
        auto result = resultRef.toObject();
        foreach (auto pairName, result.keys()) {
            auto pair = result.value(pairName).toObject();
            auto askArray = pair.value(QLatin1String("a"));
            if (askArray.isArray()) {
                auto price = askArray.toArray().at(0);
                double currentPrice = -1;
                if (price.isDouble())
                    currentPrice = price.toDouble(currentPrice);
                else
                    currentPrice = price.toString().toDouble();
                if (currentPrice > 0) {
                    setPrice(qRound(currentPrice *= 100.));
                    for (QPointer<Payment> p : m_payments) {
                        if (!p.isNull())
                            p->setExchangeRate(m_price);
                    }
                    m_payments.clear();
                }
            }
        }
    }
}

bool ExchangeRateResolver::fetchKraken()
{
    if (m_lastFetch.addSecs(300) > QDateTime::currentDateTimeUtc())
        return false;
    QNetworkRequest request(QUrl("https://api.kraken.com/0/public/Ticker?pair=bcheur"));
    m_reply = m_manager->get(request);
    connect (m_reply, SIGNAL(finished()), this, SLOT(finishedKrakenFetch()));
    return true;
}

