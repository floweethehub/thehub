/*
 * This file is part of the Flowee project
 * Copyright (C) 2019-2021 Tom Zander <tom@flowee.org>
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

#ifndef FLOWEESERVICEAPPLICATION_H
#define FLOWEESERVICEAPPLICATION_H

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QList>

#include <Logger.h>
#include <NetworkEndPoint.h>
#include <boost/asio/ip/tcp.hpp>

void HandleSIGTERM(int);
void HandleSIGHUP(int);

class QTcpServer;

/**
 * The FloweeServiceApplication is a Qt-CoreApplication inheriting application instance.
 * This class adds integration with Flowee components like the network manager and the logging
 * subsystem while keeping the Qt style of working,  for instance it uses the Qt command line
 * parser.
 *
 * There are two 'modes' you can start the app in, either as intended for a headless server
 * or as a CLI tool (typically a client).
 *
 * @code
 *  FloweeServiceApplication app(argc, argv);
    app.setOrganizationName("MyComapny");
    app.setApplicationName("myApp");

    QCommandLineParser parser;
    parser.setApplicationDescription("Its awesome");
    parser.addHelpOption(); // allows users to get an overview of options
    QCommandLineOption conf(QStringList() << "conf", "config file", "FILENAME"); // example config
    parser.addOption(conf);
    // this example is a server, lets add some FloweeServiceAplication own user-options.
    app.addServerOptions(parser, FloweeServiceApplication::NoConnect);
    parser.process(app.arguments());

    // Now allow the FloweeServiceApplication to process the user-options
    app.setup("my.log", parser.value(conf));
 * @endcode
 */
class FloweeServiceApplication : public QCoreApplication {
    Q_OBJECT
public:
    /**
     *  The constructor forwards he argc/argv to QCoreApplication and takes a log section.
     *  The Flowee Logger uses sections, typically one per library or app. By supplying a
     *  default section here, all the logging done by this class will be done in that same
     *  section.
     *
     *  We advice doing a CMAKE define for LOG_DEFAULT_SECTION to your apps default section integer.
     */
    FloweeServiceApplication(int &argc, char **argv, short appLogSection = LOG_DEFAULT_SECTION);
    ~FloweeServiceApplication();

    enum Option {
        NoOptions = 0,
        NoConnect = 1,
        NoVerbosity = 2
    };
    Q_DECLARE_FLAGS(Options, Option)


    /// If the app is meant to be a server or service, call this.
    void addServerOptions(QCommandLineParser &parser, Options options = NoOptions);
    /// If the app is meant to be a CLI tool, call this.
    void addClientOptions(QCommandLineParser &parser, Options options = NoOptions);
    /// After the QCommandLineParser::process has been called, please call setup()
    void setup(const char *logFilename = nullptr, const QString &configFilePath = QString());

    /// Clients that connect to a server can call this to fetch a parsed EndPoint of the server
    EndPoint serverAddressFromArguments(uint16_t defaultPort) const;

    enum DefaultBindOption {
        UserSupplied,           ///< If the user doesn't supply a bind option, we don't bind.
        LocalhostAsDefault,     ///< If no user supplied bind was found, we bind to localhost (ipv4 and ipv6)
        AllInterfacesAsDefault  ///< If no user supplied bind was found, we bind to all found interfaces.
    };

    /**
     * Return all end points based on the command line arguments.
     * We accept "localhost" as a string to bind to that.
     *
     * We accept "0.0.0.0" as a wildcard to all local interfaces. Please note this requires QtNetworkLib.
     */
    QList<boost::asio::ip::tcp::endpoint> bindingEndPoints(QCommandLineParser &parser, uint16_t defaultPort, DefaultBindOption defaultBind = UserSupplied) const;

    /// A server that bound SIGHUP will want to call this in the handler to re-create logfiles and such.
    void handleSigHub() const;

    int bindTo(QTcpServer *server, int defaultPort);

signals:
    void reparseConfig() const;

private:
    QCommandLineOption m_debug;
    QCommandLineOption m_verbose;
    QCommandLineOption m_quiet;
    QCommandLineOption m_version;
    QCommandLineOption m_bindAddress;
    QCommandLineOption m_connect;
    QString m_logsconf;
    QString m_logFile;
    short m_appLogSection = -1;
    bool m_isServer = false;

    QCommandLineParser *m_parser = nullptr;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(FloweeServiceApplication::Options)

#endif
