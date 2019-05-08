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
#include <base58.h>

#include <QCoreApplication>
#include <QDebug>
#include <qfile.h>

#include <streaming/MessageParser.h>

NetworkPaymentProcessor::NetworkPaymentProcessor(NetworkConnection && connection, QObject *parent)
    : QObject(parent),
    m_connection(std::move(connection))
{
    m_connection.setOnConnected(std::bind(&NetworkPaymentProcessor::connectionEstablished, this, std::placeholders::_1));
    m_connection.setOnIncomingMessage(std::bind(&NetworkPaymentProcessor::onIncomingMessage, this, std::placeholders::_1));
    m_connection.connect();
}

void NetworkPaymentProcessor::onIncomingMessage(const Message &message)
{
    Streaming::MessageParser::debugMessage(message);
    Streaming::MessageParser parser(message.body());
    if (message.serviceId() == Api::APIService) {
        if (message.messageId() == Api::Meta::VersionReply) {
            while (parser.next() == Streaming::FoundTag) {
                if (parser.tag() == Api::Meta::GenericByteData) {
                    if (!parser.isString()) {
                        logFatal() << "Unexpected reply from server-handshake. Shutting down";
                        ::exit(1);
                    }
                    logCritical() << "Remote server version:" << parser.stringData();
                    if (parser.stringData().compare("Flowee:1 (2019-5.1)") <= 0) {
                        logFatal() << "Hub server is too old";
                        ::exit(1);
                    }
                }
            }
        }
        // TODO errors
    }
    else if (message.serviceId() != Api::AddressMonitorService) {
        return;
    }
    if (message.messageId() == Api::AddressMonitor::SubscribeReply) {
        auto type = parser.next();
        int result = 1;
        std::string error;
        while (type == Streaming::FoundTag) {
            if (parser.tag() == Api::AddressMonitor::Result)
                result = parser.boolData() ? 1 : 0;
            if (parser.tag() == Api::AddressMonitor::ErrorMessage)
                error = parser.stringData();

            type = parser.next();
        }
        logInfo(Log::POS) << "Subscribe added;" << result << "addresses";
        if (!error.empty())
            logCritical(Log::POS) << "Subscribe reported error:" << error;
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
    CBase58Data old; // legacy address encoding
    if (old.SetString(address.toStdString()) && old.isMainnetPkh() || old.isMainnetSh()) {
        m_listenAddresses.append(old.data());
    } else {
        CashAddress::Content c = CashAddress::decodeCashAddrContent(address.toStdString(), "bitcoincash");
        if (c.type == CashAddress::PUBKEY_TYPE && c.hash.size() == 20) {
            m_listenAddresses.append(c.hash);
        }
        else {
            logCritical() << "Address could not be parsed";
            return;
        }
    }

    if (m_connection.isConnected()) {
        m_pool.reserve(100);
        Streaming::MessageBuilder builder(m_pool);
        assert(!m_listenAddresses.isEmpty());
        assert(m_listenAddresses.last().size() == 20);
        builder.addByteArray(Api::AddressMonitor::BitcoinAddress, m_listenAddresses.last().data(), 20);
        m_connection.send(builder.message(Api::AddressMonitorService, Api::AddressMonitor::Subscribe));
    }
}

void NetworkPaymentProcessor::connectionEstablished(const EndPoint&)
{
    logInfo(Log::POS) << "Connection established";
    m_connection.send(Message(Api::APIService, Api::Meta::Version));
    // Subscribe to the service.
    for (auto address : m_listenAddresses) {
        m_pool.reserve(100);
        Streaming::MessageBuilder builder(m_pool);
        assert(address.size() == 20);
        builder.addByteArray(Api::AddressMonitor::BitcoinAddress, address.data(), 20);
        m_connection.send(builder.message(Api::AddressMonitorService, Api::AddressMonitor::Subscribe));
    }
}

