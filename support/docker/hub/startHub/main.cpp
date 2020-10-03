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
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>
#include <QProcess>
#include <qdebug.h>

#include <unistd.h>
#include <signal.h>
#include <QTime>
#include <QDateTime>
#include <QElapsedTimer>
/*
   Interprets env vars;
   FLOWEE_NETWORK:
      regtest, testnet, testnet4 or scalenet
   FLOWEE_RPC_PASSWORD
      the password (cookie data) for the RPC access.
    FLOWEE_LOGLEVEL
        words like info, quiet or silent allowed
    FLOWEE_HUB_REINDEX
        if variable is present, we send a 'reindex' request to the hub, causing it to completely start a new block indexing (long!).
        Notice that the hub remembers this accross restarts until the reindex is finished.
 */

static QProcess *hub = new QProcess();
static bool shutdownRequested = false;

void HandleSignals(int) {
    qWarning() << "Docker: TERM received";
    shutdownRequested = true;
    qint64 pid = hub->processId();
    if (pid > 0)
        kill(pid, SIGTERM); // politely tell the Hub to terminate
}

int main(int x, char**y) {
    QCoreApplication app(x,y);

    QDir data("/data");
    if (!data.exists()) {
        qWarning() << "No volume found on /data, refusing to start";
        qWarning() << "";
        qWarning() << "The Hub prefers SSD based storage on /data";
        qWarning() << "To keep space-usage on that volume down, you can optionally also provide a";
        qWarning() << "volume on /blocks, which can be HHD";
        qWarning() << "";
        qWarning() << "The blocks will take approx 200GB, the rest will be approx 15GB.";
        return 1;
    }

    const QDir confDir(QDir::homePath() + "/.config/flowee");
    confDir.mkpath(".");
    QDir confDir2(confDir);

    const char * network = getenv("FLOWEE_NETWORK");
    QString net;
    QString subdir;
    if (network) {
        QString networkSpec = QString::fromLocal8Bit(network);
        if (networkSpec.compare("regtest", Qt::CaseInsensitive) == 0) {
            net = "regtest=true";
            subdir = "regtest";
        } else if (networkSpec.compare("testnet", Qt::CaseInsensitive) == 0
                   || networkSpec.compare("testnet3", Qt::CaseInsensitive) == 0) {
            net = "testnet=true";
            subdir = "testnet3";
        }
        else if (networkSpec.compare("testnet4", Qt::CaseInsensitive) == 0) {
            net = "testnet4=true";
            subdir = "testnet4";
        }
        else if (networkSpec.compare("scalenet", Qt::CaseInsensitive) == 0) {
            net = "scalenet=true";
            subdir = "scalenet";
        }

        if (!subdir.isEmpty()) {
            data.mkdir(subdir);
            confDir.mkdir(subdir);
            confDir2.cd(subdir);
        }
    }

    // if user mounted a volume 'blocks' then make sure they actually go there.
    if (QDir("/blocks").exists() && subdir != "regtest") {
        QDir blocks(data.absolutePath() + "/" + subdir + "/blocks");
        const bool isSymlink = !QFile::symLinkTarget(blocks.absolutePath()).isEmpty();
        if (!isSymlink) {
            if (blocks.exists())
                blocks.removeRecursively();

            int rc = symlink("/blocks", blocks.absolutePath().toLatin1().data());
            if (rc != 0)
                qWarning() << "Symlink /blocks failed";
        }
    }

    confDir2.mkdir("cookies");
    const char *rpcPassword = getenv("FLOWEE_RPC_PASSWORD");
    if (rpcPassword) {
        QFile cookie(confDir2.absolutePath() + "/cookies/hub-rpc.cookie");
        if (!cookie.open(QIODevice::WriteOnly)) {
            qWarning() << "failed to write cookie";
        } else {
            cookie.write("__cookie__:");
            cookie.write(rpcPassword, strlen(rpcPassword));
        }
    }

    QFile configFile(confDir.absolutePath() + "/flowee.conf");
    if (configFile.exists()) {
        qWarning() << "Not changing existing flowee.conf" << confDir.absoluteFilePath("flowee.conf");
    }
    else if (!configFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Can't write flowee.conf file";
    }
    else {
        QTextStream out(&configFile);
        out << "# autogenerated flowee hub config\n"
               "datadir=/data/\n"
               "pid=/run/lock/hub.pid\n"
               "maxmempool=70\n"
               "mempoolexpiry=6\n"
               "apilisten=0.0.0.0\n"
               "min-thin-peers=0\n";
        out << net << "\n\n";
        out << "# This tells hub to accept JSON-RPC commands, from anywhere, with password as stored in cookie\n"
               "server=true\n"
               "rpcallowip=127.0.0.0/0\n"
               "rpccookiefile=" + confDir2.absolutePath() + "/cookies/hub-rpc.cookie\n";
        configFile.close();
    }

    QFile logsFile(confDir2.absolutePath() + "/logs.conf");
    QString logLevel = getenv("FLOWEE_LOGLEVEL");
    if (logLevel.isEmpty() && logsFile.exists()) {
        qWarning() << "Not changing existing logs.conf" << confDir2.absoluteFilePath("logs.conf");;
    }
    else if (!logsFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Can't write logs.conf file";
    }
     else {
        QTextStream out(&logsFile);
        out << "# Flowee logging config.\n"
               "channel console\n"
               "  option timestamp time millisecond\n"
               "channel file\n"
               "  option timestamp time millisecond\n"
               "  option path /data/" << subdir << "/hub.log\n";

        if (logLevel.toLower() == "info")
            out << "\nALL info\n";
        else if (logLevel.toLower() == "quiet")
            out << "\nALL quiet\n";
        else if (logLevel.toLower() == "silent")
            out << "\nALL silent\n";
        else if (!logLevel.isEmpty())
            qWarning() << "FLOWEE_LOGLEVEL not understood. Options are 'info', 'quiet' or 'silent'";
        logsFile.close();
    }

    struct sigaction sa;
    sa.sa_handler = HandleSignals;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    // Ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);

    QStringList args;
    args << "-conf=" + confDir.absolutePath() + "/flowee.conf";
    if (getenv("FLOWEE_HUB_REINDEX"))
        args << "-reindex=true";
    hub->setReadChannel(QProcess::StandardOutput);
    QElapsedTimer startTime;
    startTime.start();
    hub->start(QLatin1String("/usr/bin/hub"), args, QIODevice::ReadOnly);
    hub->waitForReadyRead(20000);
    if (hub->state() == QProcess::NotRunning) { // time out
        qWarning() << "ERROR: hub fails to start, timing out";
        hub->kill();
        return 1;
    }
    QTextStream out(stdout);
    int rc = 0;
    while (true) {
        auto logData = hub->readAllStandardError();
        out << logData;
        logData = hub->readAllStandardOutput();
        out << logData;
        if (hub->state() != QProcess::Running) {
            rc = hub->exitCode();
            if (!shutdownRequested && rc != 0) { // maybe it crashed and we want to auto-restart it.
                const auto now = QDateTime::currentDateTimeUtc().toString();
                out << now;
                if (hub->error() == QProcess::Crashed)
                    out << " ERROR: Hub crashed due to signal " << rc << endl;
                else
                    out << " WARN: Hub exited with code " << rc << endl;

                if (startTime.elapsed() > 120000) { // but not if it crashed too fast after restart.
                    out << now << " WARN: StartHub attempts to restart hub." << endl;
                    startTime.start();
                    hub->start(QLatin1String("/usr/bin/hub"), args, QIODevice::ReadOnly);
                }
                else {
                    out << now << " WARN: StartHub detected hub restarting too fast ("
                        << int(startTime.elapsed() / 1000) << " s). Exiting" << endl;
                    break;
                }
            }
            else break;
        }
        out.flush();
        hub->waitForReadyRead(20000);
    }
    out.flush();
    fflush(nullptr);
    sync();

    return rc;
}
