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

#include "PaymentDataProvider.h"
#include "Calculator.h"
#include "QRCreator.h"
#include <Logger.h>
#include <clientversion.h>
#include <NetworkManager.h>
#include <netbase.h>

#include <qguiapplication.h>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QStandardPaths>
#include <qtextstream.h>

#include <signal.h>
#include <boost/asio/ip/address_v4.hpp>

void HandleSIGTERM(int) {
    QCoreApplication::quit();
}

int main(int x, char **y)
{
    QGuiApplication app(x, y);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("cashier");

    QString logsconf = QStandardPaths::locate(QStandardPaths::AppConfigLocation, "logs.conf");
    QString logFile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cashier.log";
    Log::Manager::instance()->parseConfig(logsconf.toLocal8Bit().toStdString(), logFile.toLocal8Bit().toStdString());

    QCommandLineParser parser;
    QCommandLineOption connect("connect", "server location and port", "<ADDERSS>");
    QCommandLineOption debug(QStringList() << "debug", "Use debug level logging");
    QCommandLineOption version(QStringList() << "version", "Display version");
    parser.addOption(connect);
    parser.addOption(debug);
    parser.addOption(version);
    parser.addHelpOption();
    parser.process(app);

    if (parser.positionalArguments().size() > 1)
        parser.showHelp(1);

    if (parser.isSet(debug)) {
        auto *logger = Log::Manager::instance();
        logger->clearChannels();
        logger->clearLogLevels(Log::DebugLevel);
        logger->addConsoleChannel();
    }
    if (parser.isSet(version)) {
        QTextStream out(stdout);
        out << app.applicationName() << " " << FormatFullVersion().c_str() << endl;
        out << "License GPLv3+: GNU GPL version 3 or later" << endl;
        out << "This is free software: you are free to change and redistribute it." << endl << endl;

        return 0;
    }

    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    signal(SIGPIPE, SIG_IGN);

    if (parser.isSet(connect)) {
        std::string hostname;
        uint16_t port = -1;
        SplitHostPort(parser.value(connect).toStdString(), port, hostname);
        QSettings settings;
        settings.beginGroup(HubConfig::GROUP_ID);
        settings.setValue(HubConfig::KEY_SERVER_PORT, port);
        settings.setValue(HubConfig::KEY_SERVER_HOSTNAME, QString::fromStdString(hostname));
    }

    qmlRegisterType<Calculator>("org.flowee", 1, 0, "Calculator");
    qmlRegisterSingletonType<PaymentDataProvider>("org.flowee", 1, 0, "Payments", [](QQmlEngine *engine, QJSEngine *scriptEngine) -> QObject* {
        Q_UNUSED(scriptEngine)
        PaymentDataProvider *dp = new PaymentDataProvider(engine);
        QRCreator *qr = new QRCreator(dp);
        engine->addImageProvider("qr", qr);
        return dp;
    });

    QQmlApplicationEngine engine;
    engine.load(QUrl(QLatin1String("qrc:/qml/mainwindow.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
