/*
 * This file is part of the bitcoin-classic project
 * Copyright (C) 2016,2019 Tom Zander <tomz@freedommail.ch>
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
#include "TxVulcano.h"

#include <Application.h>
#include <api/APIProtocol.h>
#include <primitives/FastBlock.h>
#include <TransactionBuilder.h>

#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

#include <boost/algorithm/hex.hpp>
#include <algorithm>
#include <base58.h>
#include <random>

#define MIN_FEE 2500

TxVulcano::TxVulcano(boost::asio::io_service &ioService)
    : m_networkManager(ioService),
      m_transactionsToCreate(50000),
      m_timer(ioService),
      m_wallet("mywallet")
{
}

void TxVulcano::tryConnect(const EndPoint &ep)
{
    m_connection = std::move(m_networkManager.connection(ep));
    if (!m_connection.isValid())
        throw std::runtime_error("Invalid Endpoint, can't create connection");
    m_connection.setOnConnected(std::bind(&TxVulcano::connectionEstablished, this, std::placeholders::_1));
    m_connection.setOnDisconnected(std::bind(&TxVulcano::disconnected, this));
    m_connection.setOnIncomingMessage(std::bind(&TxVulcano::incomingMessage, this, std::placeholders::_1));
    m_connection.connect();
}

void TxVulcano::connectionEstablished(const EndPoint &)
{
    logCritical() << "Connection established";
    assert(m_connection.isValid());

    // send login message
    // TODO upgrade when we have a better way to do authentication
    m_pool.reserve(70);
    Streaming::MessageBuilder builder(m_pool);
    builder.add(Api::Login::CookieData, "Vqlxrny7AVOERQg4DSMmA/XwbMYCEAZ2k8kmgBI6FaI=");
    m_connection.send(builder.message(Api::LoginService, Api::Login::LoginMessage));

    // fill the wallet with private keys
    int count = 100 - m_wallet.keyCount();
    while (--count > 0) {
        m_connection.send(Message(Api::UtilService, Api::Util::CreateAddress));
    }

    if (m_wallet.lastCachedBlock().IsNull()) {
        m_lastSeenBlock = 0; // from genesis
    } else {
        builder.add(Api::BlockChain::BlockHash, m_wallet.lastCachedBlock());
        m_connection.send(builder.message(Api::BlockChainService, Api::BlockChain::GetBlockHeader));
    }
    m_connection.send(builder.message(Api::BlockNotificationService, Api::BlockNotification::Subscribe));
    m_connection.send(builder.message(Api::BlockChainService, Api::BlockChain::GetBlockCount));
}

void TxVulcano::disconnected()
{
    logCritical() << "TxVulcano::disconnect received";
    Application::quit(0);
}

void TxVulcano::incomingMessage(const Message& message)
{
    // logDebug() << message.serviceId() << message.messageId() << message.body().size();
    if (message.serviceId() == Api::ControlService && message.messageId() == Api::Control::CommandFailed) {
        logCritical() << "incoming message recived a \"message failed\" notification";
        Streaming::MessageParser::debugMessage(message);
        Streaming::MessageParser parser(message.body());
        int serviceId = -1;
        int messageId = -1;
        std::string errorMessage;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::Control::FailedCommandServiceId)
                serviceId = parser.intData();
            else if (parser.tag() == Api::Control::FailedCommandId)
                messageId = parser.intData();
            else if (parser.tag() == Api::Control::FailedReason)
                errorMessage = parser.stringData();
        }
        if (serviceId == Api::RawTransactionService && messageId == Api::RawTransactions::SendRawTransaction) {
            int requestId = message.headerInt(Api::RequestId);

            auto iter = m_transactionsInProgress.find(requestId);
            if (iter != m_transactionsInProgress.end()) {
                if (errorMessage == "16: missing-inputs") {
                    // our wallet currently doesn't mark as spent outputs spent in the same block.
                    // so we just gracefully let the Hub do this check for now as I'm a lazy programmer.

                    m_transactionsInProgress.erase(iter);
                    return;
                }
                // if it failed because of a different reason, we assume a generate() will fix that.
                m_transactionsInProgress.clear();
                m_wallet.clearUnconfirmedUTXOs();

                // generate();
                m_timer.cancel();
                m_timer.expires_from_now(boost::posix_time::milliseconds(500));
                m_timer.async_wait(std::bind(&TxVulcano::generate, this, 1));
            }
        }
    }
    else if (message.serviceId() == Api::UtilService && message.messageId() == Api::Util::CreateAddressReply) {
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::Util::PrivateAddress) {
                CKey key;
                auto constBuf = parser.bytesDataBuffer();
                key.Set(reinterpret_cast<const unsigned char*>(constBuf.begin()),
                        reinterpret_cast<const unsigned char*>(constBuf.end()), true);

                if (key.IsValid()) {
                    m_wallet.addKey(key);
                } else  {
                    logCritical() << "Private address doesn't validate";
                }
            }
        }
    }
    else if (message.serviceId() == Api::BlockChainService && message.messageId() == Api::BlockChain::GetBlockHeaderReply) {
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::Height) {
                m_lastSeenBlock = parser.intData();
                m_highestBlock = m_lastSeenBlock;
                break;
            }
        }
    }
    else if (message.serviceId() == Api::BlockChainService && message.messageId() == Api::BlockChain::GetBlockCountReply) {
        if (m_lastSeenBlock == -1) {
            // this likely means that we had a re-org between what the wallet saw and what the server knows.
            // I think the save solution is to just exit.
            logFatal() << "My wallet and the server don't agree on block history, cowerdly refusing to continue";
            Application::quit(1);
            return;
        }
        Streaming::MessageParser parser(message.body());
        bool first = true;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::Height) {
                m_highestBlock = parser.intData();
                const auto ids = m_wallet.publicKeys();
                m_pool.reserve((m_highestBlock - m_lastSeenBlock) * 4 + ids.size() * 25);
                Streaming::MessageBuilder builder(m_pool);
                for (int i = m_lastSeenBlock + 1; i <= m_highestBlock; ++i) {
                    builder.add(Api::BlockChain::Height, i);
                    buildGetBlockRequest(builder, first);
                    m_connection.send(builder.message(Api::BlockChainService, Api::BlockChain::GetBlock));
                }
                break;
            }
        }
        if (m_highestBlock == m_lastSeenBlock)
            nowCurrent();
    }
    else if (message.serviceId() == Api::BlockChainService && message.messageId() == Api::BlockChain::GetBlockReply) {
        int txOffsetInBlock = 0;
        uint256 txid;
        int64_t amount = 0;
        int outIndex = -1;
        uint160 address;
        CScript script;
        Streaming::MessageParser parser(message.body());
        /*
         * TODO also fetch the outputs spent and remove them from the wallet
         */
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::Height) {
                m_lastSeenBlock = parser.intData();
                // logDebug() << "Block at" << m_lastSeenBlock;
            } else if (parser.tag() == Api::BlockChain::BlockHash) {
                m_wallet.setLastCachedBlock(parser.uint256Data());
            } else if (parser.tag() == Api::BlockChain::Separator) {
                txOffsetInBlock = 0;
                amount = 0;
            } else if (parser.tag() == Api::BlockChain::Tx_OffsetInBlock) {
                txOffsetInBlock = parser.intData();
            } else if (parser.tag() == Api::BlockChain::TxId) {
                txid = parser.uint256Data();
            } else if (parser.tag() == Api::BlockChain::Tx_Out_Amount) {
                amount = parser.longData();
            } else if (parser.tag() == Api::BlockChain::Tx_Script) {
                auto constBuf = parser.bytesDataBuffer();
                script = CScript(reinterpret_cast<const unsigned char*>(constBuf.begin()),
                        reinterpret_cast<const unsigned char*>(constBuf.end()));
            } else if (parser.tag() == Api::BlockChain::Tx_Out_Index) {
                outIndex = parser.intData();
            } else if (parser.tag() == Api::BlockChain::Tx_Out_Address) {
                address = base_blob<160>(parser.bytesDataBuffer().begin());
                if (txOffsetInBlock > 0)
                    logDebug() << "Got Transaction in" << m_lastSeenBlock
                                  << "@" << txOffsetInBlock
                                  << "for" << amount
                                  << "txid:" << txid
                                  << "for address" << address;
                m_wallet.addOutput(m_lastSeenBlock, txid, txOffsetInBlock, outIndex, amount, address, script);
            }
        }
        if (m_lastSeenBlock == m_highestBlock)
            this->nowCurrent();
    }
    else if (message.serviceId() == Api::RegTestService && message.messageId() == Api::RegTest::GenerateBlockReply) {
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::RegTest::BlockHash)
                logFatal() << "  Generate returns with a block hash: " << parser.uint256Data();
        }
    }
    else if (message.serviceId() == Api::BlockNotificationService && message.messageId() == Api::BlockNotification::NewBlockOnChain) {
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockNotification::BlockHash) {
                logInfo() << "Hub mined or found a new block:" << parser.uint256Data();
                m_pool.reserve(40 + m_wallet.publicKeys().size() * 25);
                Streaming::MessageBuilder builder(m_pool);
                builder.add(Api::BlockChain::BlockHash, parser.uint256Data());
                bool first = true;
                buildGetBlockRequest(builder, first);
                m_connection.send(builder.message(Api::BlockChainService, Api::BlockChain::GetBlock));
            }
            else if (parser.tag() == Api::BlockNotification::Height) {
                m_highestBlock = std::max(m_highestBlock, parser.intData());
            }
        }
    }
    else if (message.serviceId() == Api::RawTransactionService && message.messageId() == Api::RawTransactions::SendRawTransactionReply) {
        logDebug() << "SendRawTransactionReply";
        auto item = m_transactionsInProgress.find(message.headerInt(Api::RequestId));
        if (item != m_transactionsInProgress.end()) {
            UnvalidatedTransaction txData = item->second;
            const uint256 hash = txData.transaction.createHash();
            int64_t amount = -1;
            int outIndex = 0;

            Tx::Iterator iter(txData.transaction);
            while (iter.next() != Tx::End) {
                if (iter.tag() == Tx::OutputValue)
                    amount = iter.longData();
                else if (iter.tag() == Tx::OutputScript) {
                    auto constBuf = iter.byteData();
                    CScript script = CScript(reinterpret_cast<const unsigned char*>(constBuf.begin()),
                            reinterpret_cast<const unsigned char*>(constBuf.end()));
                    assert(txData.pubKeys.size() > outIndex);
                    m_wallet.addOutput(hash, outIndex, amount, txData.pubKeys.at(outIndex),
                                       txData.unconfirmedDepth + 1, script);
                    ++outIndex;
                }
            }
            m_transactionsInProgress.erase(item);
            if (outIndex > 0) {
                // we have more outputs to spent! Make sure we continue.
                m_timer.cancel();
                m_connection.postOnStrand(std::bind(&TxVulcano::createTransactions_priv, this));
            }
        }
    }
    else {
        Streaming::MessageParser::debugMessage(message);
    }
}

void TxVulcano::createTransactions(const boost::system::error_code& error)
{
    if (error)
        return;
    if (m_transactionsInProgress.size() > 50) {
        logDebug() << "Slow down...";
        // too many in flight, delay
        m_timer.expires_from_now(boost::posix_time::milliseconds(200));
        m_timer.async_wait(std::bind(&TxVulcano::createTransactions, this, std::placeholders::_1));
    } else {
        m_connection.postOnStrand(std::bind(&TxVulcano::createTransactions_priv, this));
    }
}

void TxVulcano::createTransactions_priv()
{
    logFatal() << "createTransaction";
    TransactionBuilder builder;

    /*
     * It honestly should be as simple as;
     * collect inputs and outputs and combine in a Tx...
     *
     * but...
     * I should use the wallet to fund transactions and sign it.
     * I should use the wallet to find how much money is contained in it.
     *    This means I should be able to just take the first N utxos from the wallet and base my transaction on it.
     * Next I take a list of addresses that the wallet owns and based my outputs on it.
     *
     * The wallet should have a goal number of private keys, and I should aim to use them all in 2 blocks.
     * As such I can base my number of UTXOs vs my list of private keys and decide if I want to really grow, just grow
     * or have a mostly stable ratio between inputs and outputs.
     *
     * When a transaction is signed I need to remove the UTXOs from the wallet and add the outputs it creates.
     * Additionally I should have a 'depth' int on UTXOs which states how many unconfirmed parents it has.
     *
     * Lets begin at the start;
     * * find 3 inputs
     * * find 10 target addresses
     * * create transaction
     * * make wallet sign transaction.
     * * send to Hub
     * * remove from wallet the spent UTXOs
     * * GoTO 10
     */

    /*
     * TODO
     * The storage of unconfirmed UTXOs in the wallet is a bad idea. Lots of
     * overhead there for this one usecase.
     * What about I add a list of those in this class?
     */

    short unconfirmedDepth = 0;
    int64_t amount = 0;
    for (auto utxo = m_wallet.unspentOutputs().begin(); utxo != m_wallet.unspentOutputs().end();) {
        if (utxo->coinbaseHeight > 0 && utxo->coinbaseHeight + 100 > m_highestBlock) {// coinbase maturity
            ++utxo;
            continue;
        }
        if (utxo->unconfirmedDepth > 24) {
            ++utxo;
            continue;
        }
        amount += utxo->amount;
        builder.appendInput(utxo->prevTxId, utxo->index);
        const CKey *key = m_wallet.privateKey(utxo->keyId);
        assert(key);
        builder.pushInputSignature(*key, utxo->prevOutScript, utxo->amount);
        utxo = m_wallet.spendOutput(utxo);
        unconfirmedDepth = std::max(unconfirmedDepth, utxo->unconfirmedDepth);
        break;
    }
    if (amount == 0) {
        logCritical() << "No matured coins available";
        return;
    }

    const int OutputCount = m_wallet.unspentOutputs().size() < 5000 ? 20 :
                            m_wallet.unspentOutputs().size() < 20000 ? 10 : 2;
    const int64_t outAmount = (amount - MIN_FEE) / OutputCount;

    UnvalidatedTransaction unvalidatedTransaction;
    unvalidatedTransaction.unconfirmedDepth = unconfirmedDepth;

    auto pubKeys = m_wallet.publicKeys();
    static auto engine = std::default_random_engine{};
    std::shuffle(std::begin(pubKeys), std::end(pubKeys), engine);
    int count = 0;
    for (auto out : pubKeys) {
        if (count++ == OutputCount)
            break;
        builder.appendOutput(outAmount);
        builder.pushOutputPay2Address(m_wallet.publicKey(out).GetID());
        unvalidatedTransaction.pubKeys.push_back(out);
    }

    m_pool.reserve(2000); // Should be plenty
    Tx signedTx = builder.createTransaction(&m_pool);

    m_pool.reserve(signedTx.size() + 30);
    Streaming::MessageBuilder mb(m_pool);
    mb.add(Api::RawTransactions::RawTransaction, signedTx.data());
    Message m(mb.message(Api::RawTransactionService, Api::RawTransactions::SendRawTransaction));

    const int id = ++m_lastId;
    unvalidatedTransaction.transaction = signedTx;
    m_transactionsInProgress.insert(std::make_pair(id, unvalidatedTransaction));
    ++m_transactionsCreated;
    m.setHeaderInt(Api::RequestId, id);
    m_connection.send(m);

    m_timer.cancel();
    m_timer.expires_from_now(boost::posix_time::milliseconds(1));
    m_timer.async_wait(std::bind(&TxVulcano::createTransactions, this, std::placeholders::_1));
}

std::vector<char> TxVulcano::createOutScript(const std::vector<char> &address)
{
    const uint8_t OP_DUP = 0x76;
    const uint8_t OP_HASH160 = 0xa9;
    const uint8_t OP_EQUALVERIFY = 0x88;
    const uint8_t OP_CHECKSIG = 0xac;
    std::vector<char> answer;
    answer.reserve(address.size() + 5);
    answer.push_back(OP_DUP);
    answer.push_back(OP_HASH160);
    answer.push_back((uint8_t) address.size());
    answer.insert(answer.end(), address.begin(), address.end());
    answer.push_back(OP_EQUALVERIFY);
    answer.push_back(OP_CHECKSIG);
    assert(answer.size() == address.size() + 5);
    return answer;
}

void TxVulcano::buildGetBlockRequest(Streaming::MessageBuilder &builder, bool &first) const
{
    if (first) {
        for (auto i : m_wallet.publicKeys()) {
            const CKeyID id = m_wallet.publicKey(i).GetID();
            builder.add(first ? Api::BlockChain::SetFilterAddress : Api::BlockChain::AddFilterAddress,
                        std::vector<char>(id.begin(), id.end()));
            first = false;
        }
    } else {
        builder.add(Api::BlockChain::ReuseAddressFilter, true);
    }

    builder.add(Api::BlockChain::GetBlock_TxId, true);
    builder.add(Api::BlockChain::GetBlock_OffsetInBlock, true);
    builder.add(Api::BlockChain::GetBlock_OutputAmounts, true);
    builder.add(Api::BlockChain::GetBlock_OutputAddresses, true);
    builder.add(Api::BlockChain::GetBlock_OutputScripts, true);
}

void TxVulcano::nowCurrent()
{
    logDebug() << "nowCurrent";
    if (m_wallet.unspentOutputs().size() < 10)
        generate(110);
    else
        createTransactions_priv();
}

void TxVulcano::generate(int blockCount)
{
    m_pool.reserve(30);
    Streaming::MessageBuilder builder(m_pool);
    int pkId = m_wallet.firstEmptyPubKey();
    assert(pkId >= 0);
    const CKeyID id = m_wallet.publicKey(pkId).GetID();
    builder.add(Api::RegTest::BitcoinAddress, std::vector<char>(id.begin(), id.end()));
    builder.add(Api::RegTest::Amount, blockCount);
    m_connection.send(builder.message(Api::RegTestService, Api::RegTest::GenerateBlock));
}
