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

class QTcpServer;

class FloweeServiceApplication : public QCoreApplication {
    Q_OBJECT
public:
    FloweeServiceApplication(int &argc, char **argv, short appLogSection = LOG_DEFAULT_SECTION);
    ~FloweeServiceApplication();

    enum Option {
        NoOptions = 0,
        NoConnect = 1,
        NoVerbosity = 2
    };
    Q_DECLARE_FLAGS(Options, Option)

    void addServerOptions(QCommandLineParser &parser, Options options = NoOptions);
    void addClientOptions(QCommandLineParser &parser, Options options = NoOptions);
    void setup(const char *logFilename = nullptr, const QString &configFilePath = QString());

    EndPoint serverAddressFromArguments(uint16_t defaultPort) const;

    enum DefaultBindOption {
        UserSupplied,           ///< If the user doesn't supply a bind option, we don't bind.
        LocalhostAsDefault,     ///< If no user supplied bind was found, we bind to localhost (ipv4 and ipv6)
        AllInterfacesAsDefault  ///< If no user supplied bind was found, we bind to all found interfaces.
    };

    /**
     * Return all end points based on the command line arguments.
     * We accept "localhost" as a string to bind to that.
     *
     * We accept "0.0.0.0" as a wildcard to all local interfaces. Please note this requires QtNetworkLib.
     */
    QList<boost::asio::ip::tcp::endpoint> bindingEndPoints(QCommandLineParser &parser, uint16_t defaultPort, DefaultBindOption defaultBind = UserSupplied) const;

    void handleSigHub() const;

    int bindTo(QTcpServer *server, int defaultPort);

signals:
    void reparseConfig() const;

private:
    QCommandLineOption m_debug;
    QCommandLineOption m_verbose;
    QCommandLineOption m_quiet;
    QCommandLineOption m_version;
    QCommandLineOption m_bindAddress;
    QCommandLineOption m_connect;
    QString m_logsconf;
    QString m_logFile;
    short m_appLogSection = -1;
    bool m_isServer = false;

    QCommandLineParser *m_parser = nullptr;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(FloweeServiceApplication::Options)

#endif
