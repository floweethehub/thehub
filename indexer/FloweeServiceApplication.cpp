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

#include <QCommandLineParser>
#include <QStandardPaths>
#include <netbase.h> // for SplitHostPort
#include <signal.h>

#include "Indexer.h"

void HandleSIGTERM(int) {
    QCoreApplication::quit();
}

FloweeServiceApplication::FloweeServiceApplication(int &argc, char **argv, int appLogSection)
    : QCoreApplication(argc, argv),
      m_conf(QStringList() << "conf", "Config filename", "PATH"),
      m_bindAddress(QStringList() << "bind", "Bind to this IP:port", "IP-ADDRESS"),
      m_appLogSection(appLogSection)
{
}

FloweeServiceApplication::~FloweeServiceApplication()
{
    logFatal(m_appLogSection) << "Shutdown";
}

void FloweeServiceApplication::addStandardOptions(QCommandLineParser &parser)
{
    parser.addOption(m_conf);
    parser.addOption(m_bindAddress);
}

void FloweeServiceApplication::setup(const char *logFilename) {
    // TODO check command line option
    // TODO use org-name/app-name etc to get the example dir.

    m_logsconf = QStandardPaths::locate(QStandardPaths::AppConfigLocation, "logs.conf");
    m_logFile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QString("/%1").arg(logFilename);
    Log::Manager::instance()->parseConfig(m_logsconf.toLocal8Bit().toStdString(), m_logFile.toLocal8Bit().toStdString());
    logFatal() << applicationName() << "starting.";
    if (m_logsconf.isEmpty())
        logFatal(m_appLogSection) << "No logs config found (~/.config/flowee/indexer/logs.conf), using default settings";
    else
        logFatal(m_appLogSection) << "Logs config:" << m_logsconf;

    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Reopen hub.log on SIGHUP
    struct sigaction sa_hup;
    sa_hup.sa_handler = HandleSIGHUP;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;
    sigaction(SIGHUP, &sa_hup, NULL);

    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    signal(SIGPIPE, SIG_IGN);
}

EndPoint FloweeServiceApplication::serverAddressFromArguments(QStringList args) const
{
    if (args.isEmpty()) {
        logFatal() << "No arguments given, attempting localhost:1235";
        args << "localhost:1235";
    }
    EndPoint ep;
    int port = 1234; // ep.announcePort is a short, SplitHostPort requires an int :(
    SplitHostPort(args.first().toStdString(), port, ep.hostname);
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
        try {
            answer.append(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(hostname), port));
        } catch (std::runtime_error &e) {
            logFatal().nospace() << "Bind address didn't parse: `" << address << "'. Skipping.";
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
