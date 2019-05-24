#include "BlackBoxTest.h"
#include <NetworkManager.h>

#include <QTime>

#include <signal.h>
#include <sys/types.h>

QString BlackBoxTest::s_hubPath = QString();

BlackBoxTest::BlackBoxTest(QObject *parent)
    : QObject(parent)
{
    srand(QTime::currentTime().msecsSinceStartOfDay());
    if (s_hubPath.isEmpty()) // assume running from builddir directly
        s_hubPath = QString(QT_TESTCASE_BUILDDIR "/hub/hub");
}

void BlackBoxTest::setHubExecutable(const QString &path)
{
    s_hubPath = path;
}

void BlackBoxTest::init()
{
    m_network = new NetworkManager(m_workers.ioService());
}

void BlackBoxTest::startHubs(int amount, Connect connect)
{
    Q_ASSERT(m_hubs.empty());
    Q_ASSERT(amount > 0);
    m_currentTest = QString(QTest::currentTestFunction());
    m_baseDir = QDir::tempPath() + QString("/flowee-bbtest-%1").arg(rand());
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
            "keypool=1\n"
            "server=false\n"
            "regtest=true\n"
            "apilisten=localhost:" << hub.apiPort << "\n"
            "discover=false\n";

        if (connect == ConnectHubs && i > 0)
            conf << "connect=localhost:" << hub.p2pPort - 2 << "\n\n";
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

        con.push_back(std::move(m_network->connection(EndPoint("localhost", hub.apiPort))));
        con.back().setOnIncomingMessage(std::bind(&BlackBoxTest::Hub::addMessage, &m_hubs.back(), std::placeholders::_1));
        con.back().connect();
    }
}

Message BlackBoxTest::waitForMessage(int hubId, Api::ServiceIds serviceId, int messageId, int timeout)
{
    Q_ASSERT(hubId >= 0);
    Q_ASSERT(hubId < m_hubs.size());
    QTime timer;
    timer.start();
    Hub &hub = m_hubs[hubId];
    hub.m_waitForMessageId = messageId;
    hub.m_waitForServiceId = serviceId;
    while (true) {
        Message *m = hub.m_foundMessage.load();
        if (m && m->serviceId() == serviceId && m->messageId() == messageId)
            return *m;

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
    delete m_network;
    m_network = nullptr;
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
    if (allOk)
        QDir(m_baseDir).removeRecursively();
    m_hubs.clear();
    m_currentTest.clear();
    m_baseDir.clear();
}

void BlackBoxTest::Hub::addMessage(const Message &message)
{
    // logFatal() << "addMessage" << message.serviceId() << message.messageId();
    // logFatal() << " wait: " << m_waitForServiceId  << m_waitForMessageId;
    messages.push_back(message);
    if (m_waitForServiceId != -1 && m_waitForMessageId != -1) {
        if (message.serviceId() == m_waitForServiceId && message.messageId() == m_waitForMessageId) {
            m_foundMessage.store(&messages.back());
        }
    }
}
