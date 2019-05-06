/*
 * This file is part of the Flowee project
 * Copyright (C) 2018-2019 Tom Zander <tomz@freedommail.ch>
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
#include "NetworkPaymentProcessor.h"
#include <APIProtocol.h>

#include "Logger.h"
#include <streaming/MessageBuilder.h>
#include <cashaddr.h>

#include <QCoreApplication>
#include <QDebug>
#include <qfile.h>

#include <streaming/MessageParser.h>

NetworkPaymentProcessor::NetworkPaymentProcessor(NetworkConnection && connection, QObject *parent)
    : QObject(parent),
    NetworkServiceBase(Api::AddressMonitorService),
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
        QString address;
        uint64_t amount = 0;
        auto type = parser.next();
        bool mined = false;
        while (type == Streaming::FoundTag) {
            if (parser.tag() == Api::AddressMonitor::TxId) {
                txid = parser.bytesDataBuffer();
            } else if (parser.tag() == Api::AddressMonitor::BitcoinAddress) {
                assert(parser.isByteArray());
                address = QString::fromStdString(CashAddress::encode("bitcoincash:", parser.unsignedBytesData()));
            } else if (parser.tag() == Api::AddressMonitor::Amount) {
                amount = parser.longData();
            } else if (parser.tag() == Api::AddressMonitor::OffsetInBlock) {
                // TODO maybe actually remember the offset?
                mined = true;
            }
            type = parser.next();
        }
        // TODO question of consistency, should we revert the order of the txid here, or on the server-side?
        QByteArray txIdCopy(txid.begin(), txid.size());
        for (int i = txid.size() / 2; i >= 0; --i)
            qSwap(*(txIdCopy.data() + i), *(txIdCopy.data() + txIdCopy.size() -1 - i));
        logInfo(Log::POS) << "Tx for us is" << txIdCopy.toHex().data() << " amount:" << amount << "mined:" << mined;

        emit txFound(address, txIdCopy, amount, mined);
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

