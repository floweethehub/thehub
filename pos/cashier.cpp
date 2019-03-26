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

#include "PaymentDataProvider.h"
#include "Calculator.h"
#include "QRCreator.h"

#include <Logger.h>
#include <qguiapplication.h>
#include <QCommandLineParser>

#include <QQmlApplicationEngine>
#include <QSettings>
#include <QStandardPaths>

#include <boost/asio/ip/address_v4.hpp>


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
    QCommandLineOption port("port", "Non-default port to connect to", "<number>");
    QCommandLineOption connect("connect", "IP-address to connect to, uses localhost if not given", "<IP-Address>");
    parser.addOption(port);
    parser.addOption(connect);
    parser.addHelpOption();
    parser.process(app);

    if (parser.positionalArguments().size() > 1)
        parser.showHelp(1);

    int portNum = -1;
    if (parser.isSet(port)) {
        portNum = parser.value(port).toInt();
        if (portNum == 0) {
            logFatal(Log::POS) << "Invalid port";
            return 1;
        }
    }
    QString ip;
    if (parser.isSet(connect)) {
        ip = parser.value(connect);
        EndPoint ep;
        try {
            ep.ipAddress = boost::asio::ip::address_v4::from_string(ip.toStdString());
        } catch (std::exception &) {
            try {
                ep.ipAddress = boost::asio::ip::address_v6::from_string(ip.toStdString());
            } catch (std::exception &) {
                logFatal(Log::POS) << "Failed to parse the IP address. For safety reasons we do not accept hostnames";
                return 1;
            }
        }
    }
    {
        QSettings settings;
        settings.beginGroup(HubConfig::GROUP_ID);
        if (portNum != -1)
            settings.setValue(HubConfig::KEY_SERVER_PORT, portNum);
        if (!ip.isEmpty())
            settings.setValue(HubConfig::KEY_SERVER_IP, ip);
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
