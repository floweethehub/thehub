#ifndef BLACKBOXTEST_H
#define BLACKBOXTEST_H

#include <QtTest/QtTest>
#include <QObject>

#include <NetworkConnection.h>
#include <NetworkManager.h>
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
    void cleanup(); // called after each test to clean up the started hubs

protected:
    enum Connect { ConnectHubs, Standalone };
    void startHubs(int amount = 1, Connect connect = ConnectHubs);
    /**
     * Feed a blockchain we prepared to the target hub.
     * For block 112 the tx-heights are; 81 181 1019 1855 2692 3529 4366 5203 6040 6877 7714
     */
    void feedDefaultBlocksToHub(int hubIndex);
    Message waitForReply(int hub, const Message &message, int messageId, int timeout = 30000);

    struct Hub {
        QProcess *proc;
        int p2pPort = 0;
        int apiPort = 0;
        std::deque<Message> messages;
        void addMessage(const Message &message);

        QAtomicInt m_waitForServiceId;
        QAtomicInt m_waitForMessageId;
        QAtomicInt m_waitForMessageId2;
        QAtomicPointer<Message> m_foundMessage;
    };
    std::vector<Hub> m_hubs;
    std::vector<NetworkConnection> con;
    WorkerThreads m_workers;
    NetworkManager m_network;
    QString m_currentTest;
    QString m_baseDir;
    static QString s_hubPath;
};

#endif
