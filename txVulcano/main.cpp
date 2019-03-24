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

    QCommandLineParser parser;
    parser.setApplicationDescription("Transaction generator of epic proportions");
    parser.addHelpOption();
    parser.addPositionalArgument("server", "server address with optional port");
    QCommandLineOption sizeLimit(QStringList() << "block-size" << "b", "sets a goal to the blocks-size created", "size");
    parser.addOption(sizeLimit);
    QCommandLineOption txLimit(QStringList() << "num-transactions" << "n", "Limits number of transactions created (default=5000000)", "amount");
    parser.addOption(txLimit);

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
    if (parser.isSet(sizeLimit)) {
        bool ok;
        int sl = parser.value(sizeLimit).toInt(&ok);
        if (!ok) {
            logFatal() << "size-limit has to be a number";
            return 1;
        }
        if (sl < 1) {
            logFatal() << "Min block size is 1MB";
            return 1;
        }
        vulcano.setMaxBlockSize(sl);
    }
    if (parser.isSet(txLimit)) {
        bool ok;
        int lim = parser.value(txLimit).toInt(&ok);
        if (!ok) {
            logFatal() << "num-transactions has to be a number";
            return 1;
        }
        if (lim < 1) {
            logFatal() << "num-transactions to low";
            return 1;
        }
        vulcano.setMaxNumTransactions(lim);
    }
    EndPoint ep;
    ep.announcePort = 11235;
    ep.hostname = args.first().toLocal8Bit().toStdString();
    // ep.ipAddress = boost::asio::ip::address_v4::loopback();
    vulcano.tryConnect(ep);

    return Application::instance()->exec();
}
