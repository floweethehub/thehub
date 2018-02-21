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
#ifndef HUBCONFIG_H
#define HUBCONFIG_H

#include <NetworkEndPoint.h>
#include <QObject>

class NetworkManager;

class HubConfig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString server READ server WRITE setServer NOTIFY serverChanged)
    Q_PROPERTY(QString cookieFilename READ cookieFilename WRITE setCookieFilename NOTIFY cookieFilenameChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
public:
    explicit HubConfig(QObject *parent = nullptr);

    static const char * GROUP_ID;
    static const char * KEY_SERVER_IP;
    static const char * KEY_SERVER_PORT;
    static const char * KEY_COOKIE;

    QString server() const;
    void setServer(const QString &server);

    QString cookieFilename() const;
    void setCookieFilename(const QString &cookieFilename);

    int port() const;
    void setPort(int port);

    static EndPoint readEndPoint(NetworkManager *manager, bool *ok);

signals:
    void serverChanged();
    void cookieFilenameChanged();
    void cookieConentsChanged();
    void portChanged();

private:
    QString m_server;
    QString m_cookieFilename;
    int m_port;
};

#endif // HUBCONFIG_H
