/*
 * This file is part of the Flowee project
 * Copyright (C) 2019-2021 Tom Zander <tom@flowee.org>
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

#include <utilstrencodings.h> // for SplitHostPort
#include <clientversion.h>
#include <qtextstream.h>
#include <boost/asio.hpp>
#ifdef Qt5Network_FOUND
#include <QtNetwork/QNetworkInterface>
#include <QTcpServer>
#endif

namespace {
void HandleSigTerm(int) {
    QCoreApplication::quit();
}

void HandleSigHup(int) {
    FloweeServiceApplication *app = qobject_cast<FloweeServiceApplication*>(QCoreApplication::instance());
    Q_ASSERT(app);
    app->handleSigHub();
}
}

FloweeServiceApplication::FloweeServiceApplication(int &argc, char **argv, short appLogSection)
    : QCoreApplication(argc, argv),
      m_debug(QStringList() << "debug", "Use debug level logging"),
      m_verbose(QStringList() << "verbose" << "v", "Be more verbose"),
      m_quiet(QStringList() << "quiet" << "q", "Be quiet, only errors are shown"),
      m_version(QStringList() << "version", "Display version"),
      m_bindAddress(QStringList() << "bind", "Bind to this IP:port", "IP-ADDRESS"),
      m_connect("connect", "Server location and port", "Hostname"),
      m_appLogSection(appLogSection)
{
}

FloweeServiceApplication::~FloweeServiceApplication()
{
    if (!m_logFile.isEmpty()) // only log when a logfile was passed to the setup()
        logFatal(m_appLogSection) << "Shutdown";
}

void FloweeServiceApplication::addServerOptions(QCommandLineParser &parser, Options options)
{
    m_isServer = true;
    addClientOptions(parser, options);
}

void FloweeServiceApplication::addClientOptions(QCommandLineParser &parser, Options options)
{
    m_parser = &parser;
#ifndef BCH_NO_DEBUG_OUTPUT
    if (!m_isServer)
        parser.addOption(m_debug);
#endif
    parser.addOption(m_version);
    if (!options.testFlag(NoConnect))
        parser.addOption(m_connect);
    if (m_isServer) {
        parser.addOption(m_bindAddress);
    } else if (!options.testFlag(NoVerbosity)) {
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
    if (m_parser && (!m_isServer && (m_parser->isSet(m_verbose) || m_parser->isSet(m_quiet)
#ifndef BCH_NO_DEBUG_OUTPUT
                     || m_parser->isSet(m_debug)
#endif
                     ))) {
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
        if (m_logsconf.isEmpty())
            m_logsconf = QStandardPaths::locate(QStandardPaths::ConfigLocation, "flowee/logs.conf");
        if (m_logsconf.isEmpty()) {
            logCritical().nospace() << applicationName() << "] No logs config found";
            for (auto &p : QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation)) {
                logWarning(m_appLogSection).nospace() << "  tried " << p << "/logs.conf";
            }
            for (auto &p : QStandardPaths::standardLocations(QStandardPaths::ConfigLocation)) {
                logWarning(m_appLogSection).nospace() << "  tried " << p << "/flowee/logs.conf";
            }
            logCritical().nospace() << "Log output goes to: " << m_logFile;
            Log::Manager::instance()->setLogLevel(m_appLogSection, Log::WarningLevel);
        } else {
            logCritical().nospace() << applicationName() << "] Trying logs config at " << m_logsconf;
        }

        Log::Manager::instance()->parseConfig(m_logsconf.toLocal8Bit().toStdString(), m_logFile.toLocal8Bit().toStdString());
        logFatal().nospace() << "Flowee " << applicationName() << " starting. Version: "
                                << FormatFullVersion().c_str();
        logCritical() <<  "Main Log-Section:" << m_appLogSection;
    }

    // Reopen log on SIGHUP (to allow for log-rotate)
    struct sigaction sa_hup;
    sa_hup.sa_handler = HandleSigHup;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;
    sigaction(SIGHUP, &sa_hup, nullptr);

    struct sigaction sa;
    sa.sa_handler = HandleSigTerm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);


    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    signal(SIGPIPE, SIG_IGN);
}

EndPoint FloweeServiceApplication::serverAddressFromArguments(uint16_t defaultPort) const
{
    Q_ASSERT(m_parser);
    EndPoint ep;
    ep.announcePort = defaultPort;
    if (m_parser->isSet(m_connect))
        SplitHostPort(m_parser->value(m_connect).toStdString(), ep.announcePort, ep.hostname);
    else
        ep.ipAddress = boost::asio::ip::address_v4::loopback();
    return ep;
}

QList<boost::asio::ip::tcp::endpoint> FloweeServiceApplication::bindingEndPoints(QCommandLineParser &parser, uint16_t defaultPort, DefaultBindOption defaultBind) const
{
    QStringList addresses = parser.values(m_bindAddress);
    if (addresses.isEmpty()) {
        switch (defaultBind) {
        case FloweeServiceApplication::LocalhostAsDefault:
            addresses << "localhost";
            break;
        case FloweeServiceApplication::AllInterfacesAsDefault:
            addresses << "0.0.0.0";
            break;
        case FloweeServiceApplication::UserSupplied: break;
        }
    }

    QList<boost::asio::ip::tcp::endpoint> answer;
    for (QString &address : addresses) {
        std::string hostname;
        uint16_t port = defaultPort;
        SplitHostPort(address.toStdString(), port, hostname);
        std::transform(hostname.begin(), hostname.end(), hostname.begin(), ::tolower);
        if (hostname.empty() || hostname == "localhost" || hostname == "0.0.0.0") {
            using boost::asio::ip::tcp;
            answer.push_back(tcp::endpoint(boost::asio::ip::address_v4::loopback(), port));
            answer.push_back(tcp::endpoint(boost::asio::ip::address_v6::loopback(), port));
            if (hostname == "0.0.0.0") {
#ifdef Qt5Network_FOUND
                for (auto &net : QNetworkInterface::allAddresses()) {
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
                logDebug() << e;
            }
        }
    }
    return answer;
}

void FloweeServiceApplication::handleSigHub() const
{
    Log::Manager::instance()->reopenLogFiles();
    Log::Manager::instance()->parseConfig(m_logsconf.toLocal8Bit().toStdString(), m_logFile.toLocal8Bit().toStdString());

    emit reparseConfig();
}

int FloweeServiceApplication::bindTo(QTcpServer *server, int defaultPort)
{
#ifdef Qt5Network_FOUND
    QStringList addresses = m_parser->values(m_bindAddress);
    QHostAddress address;
    int port = defaultPort;
    if (!addresses.empty()) {
        if (addresses.size() > 1) {
            logFatal() << "More than one --bind passsed, please limit to one or use 'localhost' / '0.0.0.0' wildcards";
            return 1;
        }
        QString ip(addresses.front());
        int index = ip.indexOf(":");
        if (index > 0) {
            bool ok;
            port = ip.midRef(index + 1).toInt(&ok);
            if (!ok) {
                logFatal() << "Could not parse port portion of bind address.";
                return 2;
            }
            ip = ip.left(index);
        }
        if (ip.compare("localhost", Qt::CaseInsensitive) == 0)
            address = QHostAddress::LocalHost;
        else if (ip == QLatin1String("0.0.0.0"))
            address = QHostAddress::Any;
        else {
            address = QHostAddress(ip);
        }
        if (address.isNull()) {
            logFatal() << "Did not understand bind address";
            return 2;
        }
    }
    else {
        address = QHostAddress::Any;
    }

    if (!server->listen(address, port)) {
        logCritical() << "  Failed to listen on interface";
        return 1;
    }
    return 0;
#else
    return 1; // it obviously failed if we didn't do anything.
#endif
}
