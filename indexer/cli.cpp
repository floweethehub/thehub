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
#include "IndexerClient.h"

#include <netbase.h> // for SplitHostPort

int main(int argc, char **argv)
{
    FloweeServiceApplication app(argc, argv);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("indexer-cli");

    QCommandLineParser parser;
    parser.setApplicationDescription("Indexing client");
    parser.addHelpOption();
    parser.addPositionalArgument("server", "server address");
    parser.addPositionalArgument("[TXID|ADDERSS]", "The things you want to lookup");

    QCommandLineOption hub(QStringList() << "hub", "Hub server address", "HOSTNAME");
    parser.addOption(hub);
    parser.process(app.arguments());
    auto args = parser.positionalArguments();
    if (args.isEmpty())
        parser.showHelp(1);
    if (args.size() == 1) // nothing to lookup
        return 0;

    app.setup();

    IndexerClient client;
    client.tryConnectIndexer(app.serverAddressFromArguments(args));
    for (auto a : args.mid(1)) {
        client.resolve(a);
    }

    if (parser.isSet(hub)) {
        EndPoint ep;
        int port = 1235;
        SplitHostPort(parser.value(hub).toStdString(), port, ep.hostname);
        ep.announcePort = port;
        client.tryConnectHub(ep);
    }
    return app.exec();
}
