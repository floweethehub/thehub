#include "BlackBoxTest.h"

#include <QTime>

#include <signal.h>
#include <streaming/MessageParser.h>
#include <sys/types.h>

QString BlackBoxTest::s_hubPath = QString();

BlackBoxTest::BlackBoxTest(QObject *parent)
    : QObject(parent),
      m_network(m_workers.ioService())
{
    srand(QTime::currentTime().msecsSinceStartOfDay());
    if (s_hubPath.isEmpty()) // assume running from builddir directly
        s_hubPath = QString(QT_TESTCASE_BUILDDIR "/hub/hub");
    Log::Manager::instance()->clearLogLevels(Log::DebugLevel);
}

void BlackBoxTest::setHubExecutable(const QString &path)
{
    s_hubPath = path;
}

void BlackBoxTest::startHubs(int amount, Connect connect)
{
    Q_ASSERT(m_hubs.empty());
    Q_ASSERT(amount > 0);
    m_currentTest = QString(QTest::currentTestFunction());
    m_baseDir = QDir::tempPath() + QString("/flowee-bbtest-%1").arg(rand());
    logDebug() << "Starting hub at" << m_baseDir << "with" << s_hubPath;
    int port = rand() % 31000 + 1000;
    for (int i = 0; i < amount; ++i) {
        Hub hub;
        hub.proc = new QProcess(this);
        hub.apiPort = port++;
        hub.p2pPort = port++;
        m_hubs.push_back(hub);

        QString nodePath = m_baseDir + QString("/node%1/regtest/").arg(i);
        QDir(nodePath).mkpath(".");
        QFile confFile(nodePath + "flowee.conf");
        bool ok = confFile.open(QIODevice::WriteOnly);
        Q_ASSERT(ok);
        QTextStream conf(&confFile);
        conf << "port=" << hub.p2pPort << "\n"
            "listenonion=0\n"
            "api=true\n"
            "server=false\n"
            "regtest=true\n"
            "apilisten=127.0.0.1:" << hub.apiPort << "\n"
            "discover=false\n";

        if (connect == ConnectHubs && i > 0)
            conf << "connect=127.0.0.1:" << hub.p2pPort - 2 << "\n\n";
        confFile.close();

        QFile logConfFile(nodePath + "logs.conf");
        ok = logConfFile.open(QIODevice::WriteOnly);
        Q_ASSERT(ok);
        QTextStream log(&logConfFile);
        log << "channel file\noption timestamp time\nALL debug\n2101 quiet\n#3000 quiet\n#3001 info\n";
        logConfFile.close();

        hub.proc->setProgram(s_hubPath);
        hub.proc->setWorkingDirectory(nodePath);
        hub.proc->setArguments(QStringList() << "-conf=" + nodePath + "flowee.conf" <<"-datadir=" + m_baseDir +
                               QString("/node%1").arg(i));
        hub.proc->start(QProcess::ReadOnly);
        con.push_back(std::move(m_network.connection(
                EndPoint(boost::asio::ip::address_v4::loopback(), hub.apiPort))));
        con.back().setOnIncomingMessage(std::bind(&BlackBoxTest::Hub::addMessage, &m_hubs.back(), std::placeholders::_1));
    }
}

Message BlackBoxTest::waitForMessage(int hubId, Api::ServiceIds serviceId, int messageId, int messageFailedId, int timeout)
{
    Q_ASSERT(hubId >= 0);
    Q_ASSERT(hubId < m_hubs.size());
    QTime timer;
    timer.start();
    Hub &hub = m_hubs[hubId];
    hub.m_waitForMessageId = messageId;
    hub.m_waitForServiceId = serviceId;
    hub.m_waitForMessageId2 = messageFailedId;
    hub.m_foundMessage.store(nullptr);
    while (true) {
        Message *m = hub.m_foundMessage.load();
        if (m) return *m;

        if (timeout < timer.elapsed())
            return Message();

        // if need to wait, don't burn CPU
        struct timespec tim, tim2;
        tim.tv_sec = 0;
        tim.tv_nsec = 15000;
        nanosleep(&tim , &tim2);
    }
}

void BlackBoxTest::cleanup()
{
    for (int i = 0; i < con.size(); ++i) {
        con[i].disconnect();
    }
    con.clear();
    if (m_hubs.empty()) // no hubs started
        return;
    // shut down hubs
    bool allOk = !QTest::currentTestFailed();
    for (auto hub : m_hubs) {
        auto pid = hub.proc->processId();
        Q_ASSERT(pid > 0);
        if (pid > 0)
            kill(pid, SIGTERM); // politely tell the process to die
        else
            hub.proc->kill();
    }
    for (int i = 0; i < m_hubs.size(); ++i) {
        const auto &hub = m_hubs[i];
        hub.proc->waitForFinished(10000);
        if (hub.proc->state() != QProcess::NotRunning) {
            allOk = false;
            logFatal() << m_currentTest << "Remote hub" << i << "didn't quit after 10 sec. Killing";
            hub.proc->kill();
        }
        else if (hub.proc->exitCode() != 0) {
            allOk = false;
            logFatal() << m_currentTest << "Remote hub" << i << "didn't exit cleanly. Exit code:" << hub.proc->exitCode();
        }
        else if (hub.proc->exitStatus() != QProcess::NormalExit) {
            allOk = false;
            logFatal() << m_currentTest << "Remote hub" << i << "crashed";
        }
        hub.proc->deleteLater();
    }
    if (allOk) {
        QDir(m_baseDir).removeRecursively();
    } else {
        for (int i = 0; i < m_hubs.size(); ++i) {
            QFile log(m_baseDir + QString("/node%1/regtest/hub.log").arg(i));
            if (log.open(QIODevice::ReadOnly)) {
                QTextStream in(&log);
                while (!in.atEnd()) {
                    logFatal().nospace() << "{HUB" << i << "} " << in.readLine().toLatin1().data();
                }
            }
        }
    }
    m_hubs.clear();
    m_currentTest.clear();
    m_baseDir.clear();
}

void BlackBoxTest::Hub::addMessage(const Message &message)
{
    logDebug() << "addMessage" << message.serviceId() << message.messageId() << " queue:" << messages.size();
    // logFatal() << " wait: " << m_waitForServiceId  << m_waitForMessageId;
    messages.push_back(message);
    if (m_waitForServiceId != -1 && m_waitForMessageId != -1) {
        if (message.serviceId() == m_waitForServiceId && message.messageId() == m_waitForMessageId) {
            m_foundMessage.store(&messages.back());
        }
        // also check the failed message as API Service generates.
        else if (message.serviceId() == Api::APIService && message.messageId() == Api::Meta::CommandFailed) {
            Streaming::MessageParser parser = Streaming::MessageParser(message.body());
            int ok = 0;
            while (ok < 2 && parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::Meta::FailedCommandId) {
                    if (parser.intData() != m_waitForMessageId2)
                        return;
                    ok++;
                }
                else if (parser.tag() == Api::Meta::FailedCommandServiceId) {
                    if (parser.intData() != m_waitForServiceId)
                        return;
                    ok++;
                }
            }
            if (ok == 2)
                m_foundMessage.store(&messages.back());
        }
    }
}
