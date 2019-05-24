#ifndef BLACKBOXTEST_H
#define BLACKBOXTEST_H

#include <QtTest/QtTest>
#include <QObject>

#include <NetworkConnection.h>
#include <APIProtocol.h>
#include <WorkerThreads.h>
#include <Message.h>

class QProcess;
class NetworkManager;

class BlackBoxTest : public QObject
{
    Q_OBJECT
public:
    BlackBoxTest(QObject *parent = nullptr);
    static void setHubExecutable(const QString &path);

protected slots:
    void init();
    void cleanup(); // called after each test to clean up the started hubs

protected:
    enum Connect { ConnectHubs, Standalone };
    void startHubs(int amount = 1, Connect connect = ConnectHubs);
    Message waitForMessage(int hub, Api::ServiceIds serviceId, int messageId, int timeout = 30000);

    struct Hub {
        QProcess *proc;
        int p2pPort = 0;
        int apiPort = 0;
        std::deque<Message> messages;
        void addMessage(const Message &message);

        int m_waitForServiceId = -1;
        int m_waitForMessageId = -1;
        QAtomicPointer<Message> m_foundMessage;
    };
    std::vector<Hub> m_hubs;
    std::vector<NetworkConnection> con;
    WorkerThreads m_workers;
    NetworkManager *m_network = nullptr;
    QString m_currentTest;
    QString m_baseDir;
    static QString s_hubPath;
};

#endif
