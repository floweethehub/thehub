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

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QStandardPaths>

#include <Logger.h>
#include <NetworkEndPoint.h>
#include <netbase.h> // for SplitHostPort

#include <signal.h>

#include "Indexer.h"

void HandleSIGTERM(int)
{
    QCoreApplication::quit();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("indexer");

    QCommandLineParser parser;
    parser.setApplicationDescription("Indexing server");
    parser.addHelpOption();
    parser.addPositionalArgument("server", "server address with optional port");
    QCommandLineOption datadir(QStringList() << "datadir" << "d", "The directory to put the data in", "DIR");
    parser.addOption(datadir);

    parser.process(app);
    QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        logFatal() << "No arguments given, connecting to localhost:1235";
        args << "localhost:1235";
    }

    {QString logsconf = QStandardPaths::locate(QStandardPaths::AppConfigLocation, "logs.conf");
    QString logFile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/indexer.log";
    Log::Manager::instance()->parseConfig(logsconf.toLocal8Bit().toStdString(), logFile.toLocal8Bit().toStdString());
    logFatal() << "Indexer starting. Connecting to:" << args.first();
    if (logsconf.isEmpty())
        logFatal() << "No logs config found (~/.config/flowee/indexer/logs.conf), using default settings";
    else
        logFatal() << "Logs config:" << logsconf;
    }

    EndPoint ep;
    int port = 1234; // ep.announcePort is a short, SplitHostPort requires an int :(
    SplitHostPort(args.first().toStdString(), port, ep.hostname);
    ep.announcePort = port;
    logFatal() << ep.hostname << ep.announcePort;

    QString basedir;
    if (parser.isSet(datadir))
        basedir = parser.value(datadir);
    else
        basedir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    Indexer indexer(basedir.toStdString());
    indexer.tryConnectHub(ep);

    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    return app.exec();
}
