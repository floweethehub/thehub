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
#include "FloweeServiceApplication.h"
#include <config/flowee-config.h>

#include <QCommandLineParser>
#include <QStandardPaths>
#include <signal.h>
#include <algorithm>

#include <netbase.h> // for SplitHostPort
#include <clientversion.h>
#include <qtextstream.h>
#include <boost/asio.hpp>
#ifdef Qt5Network_FOUND
#include <QtNetwork/QNetworkInterface>
#endif

void HandleSIGTERM(int) {
    QCoreApplication::quit();
}

FloweeServiceApplication::FloweeServiceApplication(int &argc, char **argv, int appLogSection)
    : QCoreApplication(argc, argv),
      m_debug(QStringList() << "debug", "Use debug level logging"),
      m_verbose(QStringList() << "verbose" << "v", "Be more verbose"),
      m_quiet(QStringList() << "quiet" << "q", "Be quiet, only errors are shown"),
      m_version(QStringList() << "version", "Display version"),
      m_bindAddress(QStringList() << "bind", "Bind to this IP:port", "IP-ADDRESS"),
      m_appLogSection(appLogSection),
      m_connect("connect", "server location and port", "<ADDERSS>")
{
}

FloweeServiceApplication::~FloweeServiceApplication()
{
    if (!m_logFile.isEmpty()) // only log when a logfile was passed to the setup()
        logFatal(m_appLogSection) << "Shutdown";
}

void FloweeServiceApplication::addServerOptions(QCommandLineParser &parser)
{
    m_isServer = true;
    addClientOptions(parser);
}

void FloweeServiceApplication::addClientOptions(QCommandLineParser &parser)
{
    m_parser = &parser;
#ifndef BCH_NO_DEBUG_OUTPUT
    parser.addOption(m_debug);
#endif
    parser.addOption(m_version);
    parser.addOption(m_connect);
    if (m_isServer) {
        parser.addOption(m_bindAddress);
    } else {
        parser.addOption(m_verbose);
        parser.addOption(m_quiet);
    }
}

void FloweeServiceApplication::setup(const char *logFilename, const QString &configFilePath) {
    if (m_parser && m_parser->isSet(m_version)) {
        QTextStream out(stdout);
        out << applicationName() << " " << FormatFullVersion().c_str() << endl;
        out << "License GPLv3+: GNU GPL version 3 or later" << endl;
        out << "This is free software: you are free to change and redistribute it." << endl << endl;

        ::exit(0);
        return;
    }
    if (m_parser && (m_parser->isSet(m_verbose) || m_parser->isSet(m_quiet)
#ifndef BCH_NO_DEBUG_OUTPUT
                     || m_parser->isSet(m_debug)
#endif
                     )) {
        auto *logger = Log::Manager::instance();
        logger->clearChannels();
        Log::Verbosity v = Log::WarningLevel;
#ifndef BCH_NO_DEBUG_OUTPUT
        if (m_parser->isSet(m_debug))
            v = Log::DebugLevel;
        else
#endif
        if (m_parser->isSet(m_verbose))
            v = Log::InfoLevel;
        else if (m_parser->isSet(m_quiet))
            v = Log::FatalLevel;
        logger->clearLogLevels(v);
        logger->addConsoleChannel();
    }
    else if (logFilename) {
        m_logsconf = QStandardPaths::locate(QStandardPaths::AppConfigLocation, "logs.conf");
        if (m_logsconf.isEmpty() && !configFilePath.isEmpty()) {
            int i = configFilePath.lastIndexOf('/');
            if (i == -1)
                m_logsconf = configFilePath + "/logs.conf";
            else
                m_logsconf = configFilePath.left(i + 1) + "logs.conf";
        }
        m_logFile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QString("/%1").arg(logFilename);
        if (m_logsconf.isEmpty()) {
            logCritical().nospace() << applicationName() << "] No logs config found";
            for (auto p : QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation)) {
                logWarning(m_appLogSection).nospace() << "  tried " << p << "/logs.conf";
            }
        } else {
            logCritical().nospace() << applicationName() << "] Trying logs config at " << m_logsconf;
        }

        Log::Manager::instance()->parseConfig(m_logsconf.toLocal8Bit().toStdString(), m_logFile.toLocal8Bit().toStdString());
        logCritical().nospace() << applicationName() << " starting. (main log-ID: " << m_appLogSection << ")";

        // Reopen log on SIGHUP (to allow for log-rotate)
        struct sigaction sa_hup;
        sa_hup.sa_handler = HandleSIGHUP;
        sigemptyset(&sa_hup.sa_mask);
        sa_hup.sa_flags = 0;
        sigaction(SIGHUP, &sa_hup, NULL);
    }

    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);


    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    signal(SIGPIPE, SIG_IGN);
}

EndPoint FloweeServiceApplication::serverAddressFromArguments(short defaultPort) const
{
    Q_ASSERT(m_parser);
    EndPoint ep;
    int port = defaultPort;
    if (m_parser->isSet(m_connect))
        SplitHostPort(m_parser->value(m_connect).toStdString(), port, ep.hostname);
    else
        ep.ipAddress = boost::asio::ip::address_v4::loopback();
    ep.announcePort = port;
    return ep;
}

QList<boost::asio::ip::tcp::endpoint> FloweeServiceApplication::bindingEndPoints(QCommandLineParser &parser, int defaultPort) const
{
    QList<boost::asio::ip::tcp::endpoint> answer;
    for (const QString &address : parser.values(m_bindAddress)) {
        std::string hostname;
        int port = defaultPort;
        SplitHostPort(address.toStdString(), port, hostname);
        std::transform(hostname.begin(), hostname.end(), hostname.begin(), ::tolower);
        if (hostname.empty() || hostname == "localhost" || hostname == "0.0.0.0") {
            using boost::asio::ip::tcp;
            answer.push_back(tcp::endpoint(boost::asio::ip::address_v4::loopback(), port));
            answer.push_back(tcp::endpoint(boost::asio::ip::address_v6::loopback(), port));
            if (hostname == "0.0.0.0") {
#ifdef Qt5Network_FOUND
                for (auto net : QNetworkInterface::allAddresses()) {
                    if (!net.isLoopback()) {
                        try {
                            answer.push_back(tcp::endpoint(boost::asio::ip::address::from_string(net.toString().toStdString()), port));
                        } catch (const std::runtime_error &e) {
                            logCritical() << "Internal error: " << e;
                        }
                    }
                }
#endif
            }
        }
        else {
            try {
                answer.append(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(hostname), port));
            } catch (std::runtime_error &e) {
                logFatal().nospace() << "Bind address didn't parse: `" << address << "'. Skipping.";
            }
        }
    }
    return answer;
}

void FloweeServiceApplication::handleSigHub() const
{
    Log::Manager::instance()->reopenLogFiles();
    Log::Manager::instance()->parseConfig(m_logsconf.toLocal8Bit().toStdString(), m_logFile.toLocal8Bit().toStdString());
}

void HandleSIGHUP(int)
{
    FloweeServiceApplication *app = qobject_cast<FloweeServiceApplication*>(QCoreApplication::instance());
    Q_ASSERT(app);
    app->handleSigHub();
}
