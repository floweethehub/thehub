#include "BlackBoxTest.h"

#include <QTime>

#include <signal.h>
#include <streaming/MessageParser.h>
#include <util.h>
#include <sys/types.h>

namespace {
void writeLogsConf(const QString &nodePath)
{
    QFile logConfFile(nodePath + "logs.conf");
    bool ok = logConfFile.open(QIODevice::WriteOnly);
    Q_ASSERT(ok);
    QTextStream log(&logConfFile);
    log << "channel file\noption timestamp time\nALL debug\n2101 quiet\n#3000 quiet\n#3001 info\n";
    logConfFile.close();
}
}

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
    Q_ASSERT(int(m_onConnectCallbacks.size()) <= amount);
    m_onConnectCallbacks.resize(amount);
    m_hubs.reserve(amount + 1);
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
            conf << "addnode=127.0.0.1:" << hub.p2pPort - 2 << "\n\n";
        confFile.close();
        writeLogsConf(nodePath);

        hub.proc->setProgram(s_hubPath);
        hub.proc->setWorkingDirectory(nodePath);
        hub.proc->setArguments(QStringList() << "-conf=" + nodePath + "flowee.conf" <<"-datadir=" + m_baseDir +
                               QString("/node%1").arg(i));
        logCritical() << "Starting hub" << hub.proc->arguments();
        hub.proc->start(QProcess::ReadOnly);
        con.push_back(m_network.connection(
                EndPoint(boost::asio::ip::address_v4::loopback(), hub.apiPort)));
        con.back().setOnIncomingMessage(std::bind(&BlackBoxTest::Hub::addMessage, &m_hubs.back(), std::placeholders::_1));
        if (m_onConnectCallbacks.at(i))
            con.back().setOnConnected(m_onConnectCallbacks.at(i));
        MilliSleep(500); // Assuming that the hub takes half a second is better than assuming it doesn't and hitting the reconnect-time.
    }
    for (int i = 0; i < amount; ++i) {
        con.at(i).connect();
    }
    logDebug() << "Hubs started";
}

void BlackBoxTest::feedDefaultBlocksToHub(int hubIndex)
{
    Q_ASSERT(m_hubs.size() > hubIndex);
    Hub &target = m_hubs[hubIndex];
    Q_ASSERT(target.proc);

    logDebug().nospace() << "Starting new hub with pre-prepared chain: node" << m_hubs.size();
    QString nodePath = m_baseDir + QString("/node%1/").arg(m_hubs.size());
    QDir(nodePath).mkpath("regtest/blocks");
    writeLogsConf(nodePath + "/regtest/");
    QFile blk(":/blk00000.dat");
    bool ok = blk.open(QIODevice::ReadOnly);
    Q_ASSERT(ok);
    ok = blk.copy(nodePath + "regtest/blocks/blk00000.dat");
    Q_ASSERT(ok);
    blk.close();
    {
        QProcess proc1;
        proc1.setProgram(s_hubPath);
        auto args = QStringList() << "-api=false" << "-server=false" << "-regtest" << "-listen=false"
                           << "-datadir=." << "-reindex" << "-stopafterblockimport";
        proc1.setArguments(args);
        logCritical() << "feedBlocks starting with:" << args;
        proc1.setWorkingDirectory(nodePath);
        proc1.start(QProcess::ReadOnly);
        proc1.waitForFinished();
    }
    logDebug() << "Reindex finished, restarting feed hub to provide the chain to node" << hubIndex;
    Hub hub;
    hub.proc = new QProcess(this);
    hub.proc->setProgram(s_hubPath);
    auto args = QStringList() << "-api=false" << "-server=false" << "-regtest"
                    << "-datadir=." << QString("-connect=127.0.0.1:%1").arg(target.p2pPort);
    hub.proc->setArguments(args);
    logCritical() << "feedBlocks restarting with" << args;
    hub.proc->setWorkingDirectory(nodePath);
    hub.proc->start(QProcess::ReadOnly);
    m_hubs.push_back(hub);

    // Ask the target hub its block-height and don't continue until it reaches that.
    NetworkConnection con = m_network.connection(
                EndPoint(boost::asio::ip::address_v4::loopback(), target.apiPort));
    con.setOnIncomingMessage(std::bind(&BlackBoxTest::Hub::addMessage, &hub, std::placeholders::_1));
    hub.m_waitForMessageId = Api::BlockChain::GetBlockCountReply;
    hub.m_waitForServiceId = Api::BlockChainService;
    hub.m_waitForMessageId2 = -1;
    for (int wait = 0; wait < 30; ++wait) {
        struct timespec tim, tim2;
        tim.tv_sec = 0;
        hub.messages.clear();
        hub.m_foundMessage.store(nullptr);
        con.send(Message(Api::BlockChainService, Api::BlockChain::GetBlockCount));
        while (true) {
            Message *m = hub.m_foundMessage.load();
            if (m) {
                Streaming::MessageParser p(m->body());
                p.next();
                if (p.tag() == Api::BlockHeight) {
                    if (p.intData() == 115) {
                        logDebug() << "  feed done, shutting down helper hub";
                        auto pid = hub.proc->processId();
                        if (pid > 0) kill(pid, SIGTERM); // politely tell the Hub to terminate
                        return;
                    }
                    logDebug() << "  hub" << hubIndex << "is at height:" << p.intData();
                    break;
                }
            }
            tim.tv_nsec = 50000;
            nanosleep(&tim , &tim2);
        }
        tim.tv_sec = 1; // give it another second.
        nanosleep(&tim , &tim2);
    }
    logFatal() << "Failed to feed chain"; // proceed, which will likely fail and we get a nice hub log
}

Message BlackBoxTest::waitForReply(int hubId, const Message &message, int messageId, int timeout)
{
    return waitForReply(hubId, message,
                        static_cast<Api::ServiceIds>(message.serviceId()), messageId, timeout);
}

Message BlackBoxTest::waitForReply(int hubId, const Message &message, Api::ServiceIds serviceId, int messageId, int timeout)
{
    Q_ASSERT(hubId >= 0);
    Q_ASSERT(hubId < int(m_hubs.size()));
    QElapsedTimer timer;
    timer.start();
    Hub &hub = m_hubs[hubId];
    hub.m_waitForMessageId = messageId;
    hub.m_waitForServiceId = serviceId;
    hub.m_waitForMessageId2 = serviceId == message.serviceId() ? message.messageId() : INT_MAX;
    hub.m_foundMessage.store(nullptr);
    con[hubId].send(message);

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

bool BlackBoxTest::waitForHeight(int height)
{
    QSet<int> nodes;
    for (int i = 0; i < (int) con.size(); ++i)
        nodes.insert(i);

    QElapsedTimer timer;
    timer.start();
    while (!nodes.isEmpty() && timer.elapsed() < 30000) {
        MilliSleep(100);
        auto copy(nodes);
        for (auto i : copy) {
            auto m = waitForReply(i, Message(Api::BlockChainService, Api::BlockChain::GetBlockCount), Api::BlockChain::GetBlockCountReply);
            if (m.serviceId() == Api::BlockChainService) { // not an error.
                Streaming::MessageParser p(m);
                p.next();
                if (p.intData() >= height)
                    nodes.remove(i);
            }
        }
    }
    return nodes.isEmpty();
}

void BlackBoxTest::cleanup()
{
    for (size_t i = 0; i < con.size(); ++i) {
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
            kill(pid, SIGTERM); // politely tell the process to terminate
        else
            hub.proc->kill();
    }
    for (size_t i = 0; i < m_hubs.size(); ++i) {
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
        for (size_t i = 0; i < m_hubs.size(); ++i) {
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
    m_onConnectCallbacks.clear();
}

void BlackBoxTest::Hub::addMessage(const Message &message)
{
    logDebug() << "addMessage" << message.serviceId() << message.messageId() << " queue:" << messages.size();
    // logFatal() << " wait: " << ((void*)this) << m_waitForServiceId.load() << m_waitForMessageId.load();
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
