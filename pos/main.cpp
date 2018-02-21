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
#include <qcoreapplication.h>
#include <QCommandLineParser>

#include <boost/asio/ip/address_v4.hpp>


int main(int x, char **y)
{
    QCoreApplication app(x, y);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("pos");

    QCommandLineParser parser;
    parser.addPositionalArgument("api-cookie", "The filename of the API cookie (secret) we can use to authenticate");
    parser.addPositionalArgument("[address]", "Addresses to listen to");
    QCommandLineOption port("port", "Non-default port to connect to", "<number>");
    QCommandLineOption connect("connect", "IP-address to connect to, uses localhost if not given", "<IP-Address>");
    parser.addOption(port);
    parser.addOption(connect);
    parser.addHelpOption();
    parser.process(app);

    WorkerThreads threads;
    NetworkManager manager(threads.ioService());
    EndPoint ep;
    if (parser.isSet(port)) {
        ep.announcePort = parser.value(port).toInt();
        if (ep.announcePort == 0) {
            logFatal(Log::POS) << "Invalid port";
            return 1;
        }
    } else {
        ep.announcePort = 1235;
    }
    if (parser.isSet(connect)) {
        try {
            ep.ipAddress = boost::asio::ip::address_v4::from_string(parser.value(connect).toStdString());
        } catch (std::exception &) {
            try {
                ep.ipAddress = boost::asio::ip::address_v6::from_string(parser.value(connect).toStdString());
            } catch (std::exception &) {
                logFatal(Log::POS) << "Failed to parse the IP address. For safety reasons we do not accept hostnames";
                return 1;
            }
        }
    }
    else
        ep.ipAddress = boost::asio::ip::address_v4::loopback();

    auto args = parser.positionalArguments();
    if (args.isEmpty())
        parser.showHelp(1);
    manager.setAutoApiLogin(true, args.takeFirst().toStdString());
    auto connection = manager.connection(ep);
    assert (connection.isValid());
    NetworkPaymentProcessor processor(std::move(connection));
    for (auto add: args) {
        processor.addListenAddress(add);
    }
    manager.addService(&processor);

    return app.exec();
}
