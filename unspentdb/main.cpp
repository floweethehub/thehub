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
#include "AbstractCommand.h"
#include "CheckCommand.h"
#include "InfoCommand.h"
#include "PruneCommand.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QStandardPaths>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("unspentdb");

    QStringList args = app.arguments();
    if (argc > 2 && args.at(1) == QLatin1String("help")) { // 'help foo' -> 'foo -h'
        args[1] = args[2];
        args[2] = QLatin1String("--help");
    }

    AbstractCommand *run = nullptr;
    if (args.size() > 1) {
        const QString command = args.at(1);
        if (command == "info")
            run = new InfoCommand();
        else if (command == "prune")
            run = new PruneCommand();
        else if (command == "check")
            run = new CheckCommand();
    }

    if (run == nullptr) {
        QTextStream out(stdout);
        out << "Usage unspentdb COMMAND [OPTIONS] ...\n" << endl;
        out << "Options:" << endl;
        out << "-f, --datafile <PATH>  <PATH> to datafile.db." << endl;
        out << "-d, --unspent <PATH>   <PATH> to unspent datadir." << endl;
        out << "-i, --info <PATH>      <PATH> to specific info file." << endl << endl;
        out << "Commands:" << endl;
        out << "  help       Display help for unspentdb or single commands." << endl;
        out << "  info       Prints generic info about a database or part of it." << endl;
        out << "  check      Checks the internal structures of the database." << endl;
        out << "  prune      Prunes spent outputs to speed up database usage." << endl;
        out << endl;
        return Flowee::InvalidOptions;
    }
    args.takeAt(1); // remove the command
    return run->start(args);
}
