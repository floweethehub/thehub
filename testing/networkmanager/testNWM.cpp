/*
 * This file is part of the Flowee project
 * Copyright (C) 2016, 2019-2020 Tom Zander <tom@flowee.org>
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
#include "testNWM.h"

#include <networkmanager/NetworkManager.h>
#include <networkmanager/NetworkManager_p.h>
#include <WorkerThreads.h>
#include <Message.h>

TestNWM::TestNWM()
{
    srand(time(nullptr));
}

void TestNWM::testBigMessage()
{
    auto localhost = boost::asio::ip::address_v4::loopback();
    const int port = std::max(1100, rand() % 32000);

    std::list<NetworkConnection> stash;
    int messageSize = -1;

    WorkerThreads threads;
    NetworkManager server(threads.ioService());
    server.bind(boost::asio::ip::tcp::endpoint(localhost, port), [&stash, &messageSize](NetworkConnection &connection) {
        connection.setOnIncomingMessage([&messageSize](const Message &message) {
            messageSize = message.body().size();
        });
        connection.accept();
        stash.push_back(std::move(connection));
    });

    NetworkManager client(threads.ioService());
    EndPoint ep;
    ep.announcePort = port;
    ep.ipAddress = localhost;
    auto con = client.connection(ep);
    con.connect();
    const int BigSize = 500000;
    Streaming::BufferPool pool(BigSize);
    for (int i =0; i < BigSize; ++i) {
        pool.data()[i] = 0xFF & i;
    }
    Message message(pool.commit(BigSize), 1);
    con.send(message);

    /*
     * This big message should be split into lots of messages but only
     * one should arrive at the other end.
     */
    QTRY_COMPARE(messageSize, BigSize);
}

void TestNWM::testRingBuffer()
{
    RingBuffer<int> buf(2000);

    QCOMPARE(buf.reserved(), 2000); // this makes sure the tests follows the implementation
    QCOMPARE(buf.isEmpty(), true);
    QCOMPARE(buf.count(), 0);
    QCOMPARE(buf.hasItemsMarkedRead(), false);
    QCOMPARE(buf.hasUnread(), false);

    for (int i = 0; i < 250; ++i) {
        buf.append(i);
    }
    QCOMPARE(buf.hasItemsMarkedRead(), false);
    QCOMPARE(buf.hasUnread(), true);
    QCOMPARE(buf.isEmpty(), false);
    QCOMPARE(buf.count(), 250);
    QCOMPARE(buf.tip(), 0);
    QCOMPARE(buf.unreadTip(), 0);

    buf.markRead(10);

    QCOMPARE(buf.hasItemsMarkedRead(), true);
    QCOMPARE(buf.isEmpty(), false);
    QCOMPARE(buf.count(), 250);
    QCOMPARE(buf.tip(), 0);
    QCOMPARE(buf.hasUnread(), true);
    QCOMPARE(buf.unreadTip(), 10);

    buf.markAllUnread();
    QCOMPARE(buf.hasItemsMarkedRead(), false);
    QCOMPARE(buf.isEmpty(), false);
    QCOMPARE(buf.count(), 250);
    QCOMPARE(buf.tip(), 0);
    QCOMPARE(buf.hasUnread(), true);
    QCOMPARE(buf.unreadTip(), 0);

    buf.markRead(249);
    QCOMPARE(buf.hasItemsMarkedRead(), true);
    QCOMPARE(buf.isEmpty(), false);
    QCOMPARE(buf.count(), 250);
    QCOMPARE(buf.tip(), 0);
    QCOMPARE(buf.hasUnread(), true);
    QCOMPARE(buf.unreadTip(), 249);

    buf.markRead(1);
    QCOMPARE(buf.hasItemsMarkedRead(), true);
    QCOMPARE(buf.isEmpty(), false);
    QCOMPARE(buf.count(), 250);
    QCOMPARE(buf.tip(), 0);
    QCOMPARE(buf.hasUnread(), false);
    // don't call unreadTip when hasUnread returns falls. It will assert.

    // remove 200 of the 250 items
    for (int i = 0; i < 200; ++i) {
        QCOMPARE(buf.hasItemsMarkedRead(), true);
        QCOMPARE(buf.isEmpty(), false);
        QCOMPARE(buf.count(), 250 - i);
        QCOMPARE(buf.tip(), i);
        QCOMPARE(buf.hasUnread(), false);
        buf.removeTip();
    }

    // add 900 items so we now have 950 items wrapping around the buffer.
    for (int i = 0; i < 900; ++i) {
        QCOMPARE(buf.hasItemsMarkedRead(), true);
        QCOMPARE(buf.isEmpty(), false);
        QCOMPARE(buf.count(), 50 + i);
        QCOMPARE(buf.tip(), 200);
        QCOMPARE(buf.hasUnread(), i != 0);
        if (i > 0)
            QCOMPARE(buf.unreadTip(), 1000);
        buf.append(1000 + i);
    }

    buf.markRead(800); // move to absolute pos 50, relative pos 850. Value 1800
    QCOMPARE(buf.hasItemsMarkedRead(), true);
    QCOMPARE(buf.isEmpty(), false);
    QCOMPARE(buf.count(), 950);
    QCOMPARE(buf.tip(), 200);
    QCOMPARE(buf.hasUnread(), true);
    QCOMPARE(buf.unreadTip(), 1800);

    // remove the first 50 items we added.
    // this means we have 900 items with value 1000 - 1900 and the read pos is at value 1800
    for (int i = 0; i < 50; ++i) {
        QCOMPARE(buf.hasItemsMarkedRead(), true);
        QCOMPARE(buf.isEmpty(), false);
        QCOMPARE(buf.count(), 950 - i);
        QCOMPARE(buf.tip(), 200 + i);
        QCOMPARE(buf.hasUnread(), true);
        QCOMPARE(buf.unreadTip(), 1800);
        buf.removeTip();
    }

    // remove all other items we added.
    for (int i = 0; i < 900; ++i) {
        QCOMPARE(buf.hasItemsMarkedRead(), i < 800);
        QCOMPARE(buf.isEmpty(), false);
        QCOMPARE(buf.count(), 900 - i);
        QCOMPARE(buf.tip(), 1000 + i);
        QCOMPARE(buf.hasUnread(), true);
        QCOMPARE(buf.unreadTip(), std::max(1800, 1000 + i));
        buf.removeTip();
    }
    // its empty now
    QCOMPARE(buf.hasItemsMarkedRead(), false);
    QCOMPARE(buf.isEmpty(), true);
    QCOMPARE(buf.count(), 0);
    QCOMPARE(buf.hasUnread(), false);
}

void TestNWM::testHeaderInt()
{
    auto localhost = boost::asio::ip::address_v4::loopback();
    const int port = std::max(1100, rand() % 32000);

    QMutex writeLock;
    std::map<int, int> headerMap;

    WorkerThreads threads;
    NetworkManager server(threads.ioService());
    std::list<NetworkConnection> stash;
    server.bind(boost::asio::ip::tcp::endpoint(localhost, port), [&stash, &headerMap, &writeLock](NetworkConnection &connection) {
        connection.setOnIncomingMessage([&headerMap, &writeLock](const Message &message) {
            QMutexLocker l(&writeLock);
            headerMap = message.headerData();
        });
        connection.accept();
        stash.push_back(std::move(connection));
    });

    NetworkManager client(threads.ioService());
    auto con = client.connection(EndPoint(localhost, port));
    const int MessageSize = 20000;
    Streaming::BufferPool pool(MessageSize);
    for (int i =0; i < MessageSize; ++i) {
        pool.data()[i] = 0xFF & i;
    }
    Message message(pool.commit(MessageSize), 1);
    message.setHeaderInt(11, 312);
    message.setHeaderInt(233, 12521);
    message.setHeaderInt(1111, 1112);
    con.send(message);
    QCOMPARE((int) message.headerData().size(), 5); // 3 from above and the service/message ids

    QTRY_COMPARE(message.headerData(), headerMap);
}

void TestNWM::testChunkReadQueue()
{
    /*
     * The NWM does flow control using the outgoing-message-queue size
     * This means we might end up pausing processing of incoming traffic in order to
     * wait for the outgoing data to be sent.
     *
     * Lets test that we still manage to send everything.
     *
     * The way to test this is simply that when we get 10 incoming messages, which generate
     * 1000 outgoing messages, then we expect the NWM to stop processing the incoming and
     * push a 'send' in between.
     */

    auto localhost = boost::asio::ip::address_v4::loopback();
    const int port = std::max(1100, rand() % 32000);

    std::list<NetworkConnection> connections;
    WorkerThreads threads;
    NetworkManager receiver(threads.ioService());
    receiver.bind(boost::asio::ip::tcp::endpoint(localhost, port), [&connections](NetworkConnection &connection) {
        connection.setMessageQueueSizes(1000, 1000);
        connections.push_back(std::move(connection));
        NetworkConnection *con = &connections.back();
        con->setOnIncomingMessage([con](const Message &message) {
            // first send a high prio, those are useful to measure the chunk-size.
            con->send(Message(1, 1), NetworkConnection::HighPriority);
            // for each incoming connection we send 100.
            for (int i = 0; i < 100; ++i) {
                con->send(Message(message.serviceId(), message.messageId() + 1));
            }
        });
        con->accept();
    });

    NetworkManager sender(threads.ioService());
    EndPoint ep;
    ep.announcePort = port;
    ep.ipAddress = localhost;
    auto con = sender.connection(ep);
    con.setMessageQueueSizes(1000, 1000);

    struct ReplyParser {
        int plainMessageCount = 0;
        int prioMessageCount = 0;

        bool ok = false;

        void replyReceived(const Message &message) {
            if (message.serviceId() == 1)
                ++prioMessageCount;
            else
                ++plainMessageCount;

            if (!ok && plainMessageCount > 300 && prioMessageCount < 5) {
                // Prio messages are sent every flush of the other side, so this is how
                // we know that the replies have been chunked. We get the first 4 batches
                // (4 + 400 messages) in one go and then we get another such batch and
                // a last batch to finish the (10 + 1000) messages count.

                // If there was no chunking we'd have gotten all prio messages
                // in one (or nothing at all).
                ok = true;
            }
        }
    };
    ReplyParser parser;
    con.setOnIncomingMessage(std::bind(&ReplyParser::replyReceived, &parser, std::placeholders::_1));
    con.connect();

    // we send 10 messages from sender to receiver.
    for (int i = 0; i < 10; ++i) {
        con.send(Message(10, 5));
    }

    QTRY_COMPARE(parser.ok, true);
}

QTEST_MAIN(TestNWM)
