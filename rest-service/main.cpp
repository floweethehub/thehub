/*
 * This file is part of the Flowee project
 * Copyright (C) 2019-2020 Tom Zander <tomz@freedommail.ch>
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

#include <QStandardPaths>

#include <FloweeServiceApplication.h>
#include <WorkerThreads.h>
#include <httpengine/server.h>
#include <Message.h>

#include "RestService.h"

#define PORT 3200

class Server : public HttpEngine::Server
{
public:
    explicit Server(const std::function<void(HttpEngine::WebRequest*)> &handler)
        : m_func(handler)
    {
    }
    HttpEngine::WebRequest *createRequest(qintptr socketDescriptor) override {
        return new RestServiceWebRequest(socketDescriptor, m_func);
    }

    void setProxy(RestService *proxy) { m_handler = proxy; }
private:
    RestService *m_handler = nullptr;
    std::function<void(HttpEngine::WebRequest*)> m_func;
};

Q_DECLARE_METATYPE(Message)

int main(int argc, char **argv)
{
    FloweeServiceApplication app(argc, argv);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("rest-service");

    QCommandLineParser parser;
    parser.setApplicationDescription("REST service");
    parser.addHelpOption();
    QCommandLineOption conf(QStringList() << "conf", "config file", "FILENAME");
    parser.addOption(conf);
    app.addServerOptions(parser);
    parser.process(app.arguments());

    app.setup("restservice.log", parser.value(conf));

    qRegisterMetaType<Message>();

    RestService handler;
    // become a server
    Server server(std::bind(&RestService::onIncomingConnection, &handler, std::placeholders::_1));
    server.setProxy(&handler);
    int rc = app.bindTo(&server, PORT);
    if (rc != 0)
        return rc;
    Q_ASSERT(server.isListening());

    try {
        auto ep = app.serverAddressFromArguments(1235);
        if (!ep.hostname.empty())
            handler.addHub(ep);
    } catch (const std::exception &e) {
        logFatal() << e;
        return 1;
    }

    QString confFile;
    if (parser.isSet(conf)) {
        confFile = parser.value(conf);
    } else {
        confFile = QStandardPaths::locate(QStandardPaths::AppConfigLocation, "rest-service.conf");
        if (confFile.isEmpty()) {
            logCritical() << "No config file (rest-service.conf) found, assuming defaults and no indexer";
            for (auto dir : QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation)) {
                logInfo() << " - not found in" << dir + '/';
            }
        }
    }
    handler.setConfigFile(confFile.toStdString());

    QObject::connect (&app, SIGNAL(reparseConfig()), &handler, SLOT(onReparseConfig()));

    return app.exec();
}
