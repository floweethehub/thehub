/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tom@flowee.org>
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

#include "IndexerClient.h"
#include <FloweeServiceApplication.h>

#include <utilstrencodings.h> // for SplitHostPort

int main(int argc, char **argv)
{
    FloweeServiceApplication app(argc, argv);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("indexer-cli");

    QCommandLineParser parser;
    parser.setApplicationDescription("Indexing client");
    parser.addHelpOption();
    parser.addPositionalArgument("[TXID|ADDRESS]", "The things you want to lookup");

    QCommandLineOption hub(QStringList() << "hub", "Hub server address", "HOSTNAME");
    parser.addOption(hub);
    app.addClientOptions(parser);
    parser.process(app.arguments());
    app.setup();

    auto args = parser.positionalArguments();
    if (args.isEmpty())
        parser.showHelp(1);

    IndexerClient client;
    if (parser.isSet(hub)) {
        EndPoint ep;
        uint16_t port = 1235;
        SplitHostPort(parser.value(hub).toStdString(), port, ep.hostname);
        ep.announcePort = port;
        client.tryConnectHub(ep);
    }
    client.tryConnectIndexer(app.serverAddressFromArguments(1234));
    for (auto &a : args) {
        client.resolve(a);
    }

    return app.exec();
}
