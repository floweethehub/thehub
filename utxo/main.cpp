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
#include <QtCore>

#include "Importer.h"

#include <Logger.h>
#include <chainparams.h>

int main(int x, char **y)
{
    QCoreApplication app(x, y);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("utxo");

    QString logsconf = QStandardPaths::locate(QStandardPaths::AppConfigLocation, "logs.conf");
    QString logFile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/utxo.log";
    Log::Manager::instance()->parseConfig(logsconf.toLocal8Bit().toStdString(), logFile.toLocal8Bit().toStdString());

    SelectParams("main");
    Importer importer;
    QTimer::singleShot(0, &importer, SLOT(start()));

    return app.exec();
}
