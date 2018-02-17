#include "NetworkPaymentProcessor.h"
#include <api/APIProtocol.h>

#include "Logger.h"
#include <streaming/MessageBuilder.h>

#include <QCoreApplication>
#include <QDebug>
#include <qfile.h>

#include <streaming/MessageParser.h>

NetworkPaymentProcessor::NetworkPaymentProcessor(NetworkConnection && connection, QObject *parent)
    : QObject(parent),
    NetworkService(Api::AddressMonitorService),
    m_connection(std::move(connection))
{
    m_connection.setOnConnected(std::bind(&NetworkPaymentProcessor::connectionEstablished, this, std::placeholders::_1));
    m_connection.connect();
}

void NetworkPaymentProcessor::onIncomingMessage(const Message &message, const EndPoint&)
{
    Streaming::MessageParser parser(message.body());
    if (message.messageId() == Api::AddressMonitor::SubscribeReply) {
        auto type = parser.next();
        int result = -1;
        std::string error;
        while (type == Streaming::FoundTag) {
            if (parser.tag() == Api::AddressMonitor::Result)
                result = parser.boolData() ? 1 : 0;
            if (parser.tag() == Api::AddressMonitor::ErrorMessage)
                error = parser.stringData();

            type = parser.next();
        }
        if (result != -1)
            logInfo(Log::POS) << "Subscribe response;" << (result == 1) << &error[0];
    }
    else if (message.messageId() == Api::AddressMonitor::TransactionFound) {
        Streaming::ConstBuffer txid;
        uint64_t amount = 0;
        auto type = parser.next();
        while (type == Streaming::FoundTag) {
            if (parser.tag() == Api::AddressMonitor::TransactionId) {
                txid = parser.bytesDataBuffer();
            } else if (parser.tag() == Api::AddressMonitor::Amount)
                amount = parser.longData();
            type = parser.next();
        }
        // TODO question of consistency, should we revert the order of the txid here, or on the server-side?
        QByteArray txIdCopy(txid.begin(), txid.size());
        for (int i = txid.size() / 2; i >= 0; --i)
            qSwap(*(txIdCopy.data() + i), *(txIdCopy.data() + txIdCopy.size() -1 - i));
        logCritical(Log::POS) << "Tx for us is" << txIdCopy.toHex().data() << " amount:" << amount;
    }
}

void NetworkPaymentProcessor::addListenAddress(const QString &address)
{
    m_listenAddresses.append(address);
    if (m_connection.isConnected()) {
        m_pool.reserve(100);
        Streaming::MessageBuilder builder(m_pool);
        builder.add(Api::AddressMonitor::BitcoinAddress, address.toStdString());
        m_connection.send(builder.message(Api::AddressMonitorService, Api::AddressMonitor::Subscribe));
    }
}

void NetworkPaymentProcessor::connectionEstablished(const EndPoint&)
{
    logInfo(Log::POS) << "Connection established";
    // Subscribe to the service.
    for (auto address : m_listenAddresses) {
        m_pool.reserve(100);
        Streaming::MessageBuilder builder(m_pool);
        builder.add(Api::AddressMonitor::BitcoinAddress, address.toStdString());
        m_connection.send(builder.message(Api::AddressMonitorService, Api::AddressMonitor::Subscribe));
    }
}

