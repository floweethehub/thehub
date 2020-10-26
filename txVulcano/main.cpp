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

#include <QCommandLineParser>
#include <WorkerThreads.h>
#include <FloweeServiceApplication.h>

// #include <Logger.h>
#include <server/chainparams.h>
#include "TxVulcano.h"

int main(int argc, char **argv)
{
    FloweeServiceApplication app(argc, argv);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("txVulcano");

    QCommandLineParser parser;
    parser.setApplicationDescription("Transaction generator of epic proportions");
    parser.addHelpOption();
    QCommandLineOption sizeLimit(QStringList() << "block-size" << "b", "sets a goal to the blocks-size created (MB)", "size");
    parser.addOption(sizeLimit);
    QCommandLineOption txLimit(QStringList() << "num-transactions" << "n", "Limits number of transactions created (default=5000000)", "amount");
    parser.addOption(txLimit);

    app.addClientOptions(parser);
    parser.process(app.arguments());
    app.setup("client.log");

    // Wallet needs this to work;
    ECC_Start();
    SelectParams("regtest");

    WorkerThreads workers;
    TxVulcano vulcano(workers.ioService());
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
    vulcano.tryConnect(app.serverAddressFromArguments(11235));

    return app.exec();
}
