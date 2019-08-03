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
    /**
     * Start as sub-processes one or more Hub instances.
     * This method will start the hubs without any history (just genesis)
     * and on the RegTest chain.
     * Note that you can only call this method once a test.
     */
    void startHubs(int amount = 1, Connect connect = ConnectHubs);
    /**
     * Feed a blockchain we prepared to the target hub.
     *
     * This method starts a new Hub with a known blockchain and connects to \a hubIndex
     * and waits until that hub is synchronized with the new one.
     *
     * For block 112 the tx-heights are; 81 181 1019 1857 2694 3531 4368 5202 6042 6879
     */
    void feedDefaultBlocksToHub(int hubIndex);

    /**
     * Send a message to hub-by-index and wait for a reply.
     * Note that the reply can be of the API service error message type as well as the
     * expected message.
     *
     * \param hub the index of the hub, started previously using startHubs()
     * \param message the actual message to send.
     * \param messageId the reply-message-id we expect.
     * \param timeout max waiting time in milliseconds.
     */
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
