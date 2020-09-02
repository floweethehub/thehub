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
#ifndef TESTAPI_H
#define TESTAPI_H

#include <QJsonValue>
#include <QJsonArray>
#include <QNetworkAccessManager>


class TestApi : public QObject
{
    Q_OBJECT
public:
    TestApi();

    void start(const QString &hostname, int port);

    QString hostname() const;
    int port() const;

public slots:
    void finishedRequest();

private:
    QNetworkAccessManager m_network;
    QString m_hostname;
    int m_port = -1;
    int m_finishedRequests = 0;
};


class AbstractTestCall : public QObject
{
    Q_OBJECT
protected:
    AbstractTestCall(QNetworkReply *parent);

    virtual void checkDocument(const QJsonDocument &doc) = 0;

    void startContext(const QString &context);
    void error(const QString &error);

    template<class V>
    inline void check(const QJsonValue &o, const QString &key, V value) {
        if (o[key] == value)
            return;
        if (o[key] == QJsonValue::Undefined) {
            static const QString failure1("%1 missing");
            error(failure1.arg(key));
        }
        else {
            static const QString failure2("%1 has incorrect value");
            error(failure2.arg(key));
        }
    }
    template<class V>
    inline void check(const QJsonArray &o, int index, V value) {
        if (o.at(index) == value)
            return;
        static const QString failure3("array[%1] has incorrect value");
        error(failure3.arg(index));
    }

signals:
    void requestDone();

private slots:
    void finished();
    void timeout();

protected:
    QNetworkReply *m_reply;
    struct Error {
        QString context;
        QString error;
    };

    QList<Error> m_errors;
    QString m_context;
};


class TestAddressDetails : public AbstractTestCall
{
    Q_OBJECT
public:
    static void startRequest(TestApi *parent, QNetworkAccessManager &manager);

protected:
    TestAddressDetails(QNetworkReply *parent) : AbstractTestCall(parent) { }

    void checkDocument(const QJsonDocument &doc) override;
};

#endif
