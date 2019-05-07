/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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
#include <netbase.h>

#include <boost/asio/ip/address_v4.hpp>


int main(int x, char **y)
{
    FloweeServiceApplication app(x, y);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("pos");

    QCommandLineParser parser;
    parser.addPositionalArgument("[address]", "Addresses to listen to");
    QCommandLineOption connect("connect", "server location and port", "<ADDERSS>");
    parser.addOption(connect);
    app.addClientOptions(parser);
    parser.process(app.arguments());
    app.setup();

    auto args = parser.positionalArguments();
    if (args.isEmpty())
        parser.showHelp(1);

    EndPoint ep;
    int port = 1235;
    if (parser.isSet(connect))
        SplitHostPort(parser.value(connect).toStdString(), port, ep.hostname);
    else
        ep.ipAddress = boost::asio::ip::address_v4::loopback();
    ep.announcePort = port;

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
