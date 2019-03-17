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
#include <WorkerThreads.h>
#include <Application.h>
#include <QStandardPaths>

#include <Logger.h>
#include <server/chainparams.h>
#include "TxVulcano.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("txVulcano");

    // server <:port>
    //  -size-limit=1MB
    //  -tx-limit=100
    //  -mine-block=blocksizeMB

    // start client, connect

    // read;
    // ~/.local/share/flowee/txVolcano.db
    // which should contain all the private and public addresses known to the vulcano.
    // if we own less than 10BCH, mine some.

    // use the previous algo to distribute the transactions.
    // notice that I need a transaction-builder class as the previous design used TxV4

    QCommandLineParser parser;
    parser.setApplicationDescription("Transaction generator of epic proportions");
    parser.addHelpOption();
    parser.addPositionalArgument("server", "server address with optional port");
    QCommandLineOption sizeLimit(QStringList() << "size-limit" << "l", "sets a limit to the amount of transactions created" );
    parser.addOption(sizeLimit);

    parser.process(app);
    const QStringList args = parser.positionalArguments();
    if (args.isEmpty())
        parser.showHelp(1);

    QString logsconf = QStandardPaths::locate(QStandardPaths::AppConfigLocation, "logs.conf");
    QString logFile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/client.log";
    Log::Manager::instance()->parseConfig(logsconf.toLocal8Bit().toStdString(), logFile.toLocal8Bit().toStdString());
    logFatal() << "TxVulcano starting. Connecting to:" << args.first();
    if (logsconf.isEmpty())
        logFatal() << "No logs config found (~/.config/flowee/txVulcano/logs.conf), using default settings";
    else
        logFatal() << "Logs config:" << logsconf;

    // Wallet needs this to work;
    ECC_Start();
    SelectParams("regtest");
    TxVulcano vulcano(Application::instance()->ioService());
    EndPoint ep;
    ep.announcePort = 11235;
    ep.hostname = args.first().toLocal8Bit().toStdString();
    // ep.ipAddress = boost::asio::ip::address_v4::loopback();
    vulcano.tryConnect(ep);

    return Application::instance()->exec();
}
