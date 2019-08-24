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

#include <QStandardPaths>

#include <FloweeServiceApplication.h>

#include "Indexer.h"

int main(int argc, char **argv)
{
    setenv("QT_NO_GLIB", "1", 1);
    FloweeServiceApplication app(argc, argv);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("indexer");

    QCommandLineParser parser;
    parser.setApplicationDescription("Indexing server");
    parser.addHelpOption();
    QCommandLineOption datadir(QStringList() << "datadir" << "d", "The directory to put the data in", "DIR");
    parser.addOption(datadir);
    QCommandLineOption conf(QStringList() << "conf", "config file", "FILENAME");
    parser.addOption(conf);
    app.addServerOptions(parser);
    parser.process(app.arguments());

    app.setup("indexer.log", parser.value(conf));

    QString basedir;
    if (parser.isSet(datadir))
        basedir = parser.value(datadir);
    else
        basedir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    Indexer indexer(basedir.toStdString());

    // become a server
    for (auto ep : app.bindingEndPoints(parser, 1234, FloweeServiceApplication::LocalhostAsDefault)) {
        logCritical().nospace() << "Trying to bind to " << ep.address().to_string().c_str() << ":" << ep.port();
        try {
            indexer.bind(ep);
        } catch (std::exception &e) {
            logCritical() << "   nope, not binding there due to:" << e;
        }
    }

    QString confFile;
    if (parser.isSet(conf))
        confFile = parser.value(conf);
    else
        confFile = QStandardPaths::locate(QStandardPaths::AppConfigLocation, "indexer.conf");
    indexer.loadConfig(confFile, app.serverAddressFromArguments(1235));
    return app.exec();
}
