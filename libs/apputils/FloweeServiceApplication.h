/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
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

#ifndef FLOWEESERVICEAPPLICATION_H
#define FLOWEESERVICEAPPLICATION_H

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QList>

#include <Logger.h>
#include <NetworkEndPoint.h>
#include <boost/asio/ip/tcp.hpp>

void HandleSIGTERM(int);
void HandleSIGHUP(int);

class FloweeServiceApplication : public QCoreApplication {
    Q_OBJECT
public:
    FloweeServiceApplication(int &argc, char **argv, int appLogSection = LOG_DEFAULT_SECTION);
    ~FloweeServiceApplication();

    void addServerOptions(QCommandLineParser &parser);
    void addClientOptions(QCommandLineParser &parser);
    void setup(const char *logFilename = nullptr, const QString &configFilePath = QString());

    EndPoint serverAddressFromArguments(short defaultPort) const;
    QList<boost::asio::ip::tcp::endpoint> bindingEndPoints(QCommandLineParser &parser, int defaultPort) const;

    void handleSigHub() const;

private:
    QCommandLineOption m_debug;
    QCommandLineOption m_verbose;
    QCommandLineOption m_quiet;
    QCommandLineOption m_version;
    QCommandLineOption m_bindAddress;
    QCommandLineOption m_connect;
    QString m_logsconf;
    QString m_logFile;
    int m_appLogSection = -1;
    bool m_isServer = false;

    QCommandLineParser *m_parser = nullptr;
};

#endif
