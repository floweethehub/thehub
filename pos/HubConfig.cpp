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
#include "HubConfig.h"

#include <NetworkManager.h>
#include <Logger.h>

#include <qsettings.h>


const char * HubConfig::GROUP_ID = "server";
const char * HubConfig::KEY_SERVER_IP = "ip";
const char * HubConfig::KEY_SERVER_PORT = "port";
const char * HubConfig::KEY_COOKIE = "ecookie_filename";

static void set(const char *key, const QVariant &value)
{
    QSettings settings;
    settings.beginGroup(HubConfig::GROUP_ID);
    settings.setValue(key, value);
}


HubConfig::HubConfig(QObject *parent) : QObject(parent), m_port(1235)
{
}

QString HubConfig::server() const
{
    return m_server;
}

void HubConfig::setServer(const QString &server)
{
    if (m_server == server)
        return;
    m_server = server;
    set(KEY_SERVER_IP, server);
    emit serverChanged();
}

QString HubConfig::cookieFilename() const
{
    return m_cookieFilename;
}

void HubConfig::setCookieFilename(const QString &cookieFilename)
{
    if (m_cookieFilename == cookieFilename)
        return;
    m_cookieFilename = cookieFilename;
    set(KEY_COOKIE, cookieFilename);
    emit cookieFilenameChanged();
}

int HubConfig::port() const
{
    return m_port;
}

void HubConfig::setPort(int port)
{
    if (m_port == port)
        return;
    m_port = port;
    set(KEY_SERVER_PORT, port);
    emit portChanged();
}

EndPoint HubConfig::readEndPoint(NetworkManager *manager, bool *ok)
{
    Q_ASSERT(manager);
    QSettings settings;
    settings.beginGroup(GROUP_ID);
    EndPoint ep;
    ep.announcePort = settings.value(KEY_SERVER_PORT, 1235).toInt();
    bool incomplete = ep.announcePort < 1;
    QString ip = settings.value(KEY_SERVER_IP, "127.0.0.1").toString();
    try {
        ep.ipAddress = boost::asio::ip::address_v4::from_string(ip.toStdString());
    } catch (std::exception &) {
        try {
            ep.ipAddress = boost::asio::ip::address_v6::from_string(ip.toStdString());
        } catch (std::exception &) {
            incomplete = true;
        }
    }
    const QString cookieFilename = settings.value(KEY_COOKIE).toString();
    logInfo(Log::POS) << "Setting cookie file to;" << cookieFilename;
    manager->setAutoApiLogin(true, cookieFilename.toStdString());
    if (ok)
        *ok = !incomplete;
    return ep;
}
