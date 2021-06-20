/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tom@flowee.org>
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
#include <NetworkManager.h>

#include "NetworkPaymentProcessor.h"

#include <WorkerThreads.h>
#include <Logger.h>
#include <FloweeServiceApplication.h>

#include <boost/asio/ip/address_v4.hpp>


int main(int x, char **y)
{
    FloweeServiceApplication app(x, y);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("pos");

    QCommandLineParser parser;
    parser.addPositionalArgument("[address]", "Addresses to listen to");
    parser.addHelpOption();
    app.addClientOptions(parser);
    parser.process(app.arguments());
    app.setup();

    auto args = parser.positionalArguments();
    if (args.isEmpty())
        parser.showHelp(1);

    EndPoint ep = app.serverAddressFromArguments(1235);
    WorkerThreads threads;
    NetworkManager manager(threads.ioService());
    auto connection = manager.connection(ep);
    assert (connection.isValid());
    NetworkPaymentProcessor processor(std::move(connection));
    for (auto add: args) {
        processor.addListenAddress(add);
    }

    return app.exec();
}
