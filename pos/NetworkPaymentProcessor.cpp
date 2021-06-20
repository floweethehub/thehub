/*
 * This file is part of the Flowee project
 * Copyright (C) 2018-2019 Tom Zander <tom@flowee.org>
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
#include <QString>
#include <Logger.h>
#include "NetworkPaymentProcessor.h"

#include <APIProtocol.h>

#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>
#include <primitives/FastTransaction.h>
#include <base58.h>


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
                    if (parser.stringData().compare("Flowee:1 (2019-5.1)") < 0) {
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
        uint64_t amount = 0;
        auto type = parser.next();
        int offsetInBlock = 0, blockheight = 0;
        while (type == Streaming::FoundTag) {
            if (parser.tag() == Api::AddressMonitor::TxId) {
                txid = parser.bytesDataBuffer();
            } else if (parser.tag() == Api::AddressMonitor::BitcoinScriptHashed) {
                assert(parser.isByteArray());
            } else if (parser.tag() == Api::AddressMonitor::Amount) {
                amount = parser.longData();
            } else if (parser.tag() == Api::AddressMonitor::OffsetInBlock) {
                offsetInBlock = parser.intData();
            } else if (parser.tag() == Api::AddressMonitor::BlockHeight) {
                blockheight = parser.intData();
            }
            type = parser.next();
        }
        if (blockheight > 0) {
            logCritical(Log::POS) << "Hub mined a transation paying us."
                                  << "Block:" << blockheight << "offset:"
                                  << offsetInBlock << "Amount (sat):" << amount;
        } else if (txid.size() == 32) {
            uint256 hash(txid.begin());
            logCritical(Log::POS) << "Hub recived (mempool) a transaction transation paying us."
                                  << "txid:" << hash << "Amount (sat):" << amount;

        } else {
            logCritical(Log::POS) << "Hub sent TransactionFound message that looks to be missing data";
            Streaming::MessageParser::debugMessage(message);
        }
    }
    else if (message.messageId() == Api::AddressMonitor::DoubleSpendFound) {
        QString scriptHash;
        uint64_t amount = 0;
        Streaming::ConstBuffer txid, duplicateTx;
        auto type = parser.next();
        while (type == Streaming::FoundTag) {
            if (parser.tag() == Api::AddressMonitor::TxId) {
                assert(parser.isByteArray());
                txid = parser.bytesDataBuffer();
            } else if (parser.tag() == Api::AddressMonitor::TransactionData) {
                assert(parser.isByteArray());
                duplicateTx = parser.bytesDataBuffer();
            } else if (parser.tag() == Api::AddressMonitor::DoubleSpendProofData) {
                assert(parser.isByteArray());
                // duplicateTx = parser.bytesDataBuffer(); // TODO
            } else if (parser.tag() == Api::AddressMonitor::BitcoinScriptHashed) {
                assert(parser.isByteArray());
                QByteArray bytes(parser.bytesDataBuffer().begin(), parser.dataLength());
                scriptHash = QString::fromLatin1(bytes.toHex());
            } else if (parser.tag() == Api::AddressMonitor::Amount) {
                amount = parser.longData();
            }
            type = parser.next();
        }
        Tx tx(duplicateTx);
        logCritical(Log::POS) << "WARN: double spend detected on one of our monitored addresses:" << scriptHash
                              << "amount:" << amount << "tx:" << tx.createHash();
    }
}

void NetworkPaymentProcessor::addListenAddress(const QString &address)
{
    CashAddress::Content c = CashAddress::decodeCashAddrContent(address.toStdString(), "bitcoincash");
    if (c.hash.empty()) {
        CBase58Data old; // legacy address encoding
        if (old.SetString(address.toStdString())) {
            c.hash = old.data();
            if (old.isMainnetPkh()) {
                c.type= CashAddress::PUBKEY_TYPE;
            } else if (old.isMainnetSh()) {
                c.type= CashAddress::SCRIPT_TYPE;
            } else {
                logCritical() << "Address could not be parsed";
                return;
            }
        }
    }
    m_listenAddresses.append(c);

    if (m_connection.isConnected()) {
        m_pool.reserve(40);
        Streaming::MessageBuilder builder(m_pool);
        builder.add(Api::AddressMonitor::BitcoinScriptHashed, CashAddress::createHashedOutputScript(c));
        m_connection.send(builder.message(Api::AddressMonitorService, Api::AddressMonitor::Subscribe));
    }
}

void NetworkPaymentProcessor::connectionEstablished(const EndPoint&)
{
    logInfo(Log::POS) << "Connection established";
    m_connection.send(Message(Api::APIService, Api::Meta::Version));
    // Subscribe to the service.
    for (auto address : m_listenAddresses) {
        m_pool.reserve(40);
        Streaming::MessageBuilder builder(m_pool);
        builder.add(Api::AddressMonitor::BitcoinScriptHashed, CashAddress::createHashedOutputScript(address));
        m_connection.send(builder.message(Api::AddressMonitorService, Api::AddressMonitor::Subscribe));
    }
}

