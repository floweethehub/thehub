/*
 * This file is part of the Flowee project
 * Copyright (C) 2016,2019-2020 Tom Zander <tomz@freedommail.ch>
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

#include <QCoreApplication>
#include <APIProtocol.h>
#include <primitives/FastBlock.h>
#include <TransactionBuilder.h>

#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

#include <boost/algorithm/hex.hpp>
#include <algorithm>
#include <base58.h>
#include <random>

#define MIN_FEE 1000

#include <QStandardPaths>
#include <cashaddr.h>
#include <qtimer.h>

enum PrivateTags {
    LastBlockInChunk = Api::UserTag1
};

TxVulcano::TxVulcano(boost::asio::io_service &ioService, const QString &walletname)
    : m_networkManager(ioService),
      m_transactionsToCreate(5000000),
      m_transactionsCreated(0),
      m_blockSizeLeft(-1),
      m_timer(ioService),
      m_walletMutex(QMutex::Recursive),
      m_wallet(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/" + walletname)
{
    qRegisterMetaType<Message>("Message");
    moveToThread(&m_workerThread);
    m_workerThread.start();
    // connect but make sure that the processNewBlock is on the Qt thread.
    connect (this, SIGNAL(newBlockFound(Message)), SLOT(processNewBlock(Message)), Qt::QueuedConnection);

    setMaxBlockSize(50);
}

TxVulcano::~TxVulcano()
{
    m_workerThread.exit(0);
    m_workerThread.wait();
}

void TxVulcano::tryConnect(const EndPoint &ep)
{
    m_connection = m_networkManager.connection(ep);
    if (!m_connection.isValid())
        throw std::runtime_error("Invalid Endpoint, can't create connection");
    m_connection.setOnConnected(std::bind(&TxVulcano::connectionEstablished, this, std::placeholders::_1));
    m_connection.setOnDisconnected(std::bind(&TxVulcano::disconnected, this));
    m_connection.setOnIncomingMessage(std::bind(&TxVulcano::incomingMessage, this, std::placeholders::_1));
    m_connection.connect();
}

void TxVulcano::setMaxBlockSize(int sizeInMb)
{
    m_nextBlockSize.clear();
    int sequence[] { 0, 20, 50, 100, 250, 600, 1000, 1400, 1900, -1 };
    for (int i = 1; sequence[i] > 0 && sequence[i - 1] <= sizeInMb; ++i) {
        const int size = std::min(sizeInMb, sequence[i]);
        for (int n = 0; n < 5; ++n) {
            m_nextBlockSize.append(size);
        }
    }
    assert(!m_nextBlockSize.isEmpty());
    m_blockSizeLeft = m_nextBlockSize.takeFirst() * 1000000;
    m_lastPrintedBlockSizeLeft = m_blockSizeLeft;
    logCritical() << "Setting block size wanted to" << (m_blockSizeLeft / 1000000) << "MB";
}

void TxVulcano::connectionEstablished(const EndPoint &)
{
    logCritical() << "Connection established";
    assert(m_connection.isValid());
    m_serverSupportsAsync = false;
    m_connection.send(Message(Api::APIService, Api::Meta::Version));

    QMutexLocker lock(&m_walletMutex);
    // fill the wallet with private keys
    int count = 100 - m_wallet.keyCount();
    Message createAddressRequest(Api::UtilService, Api::Util::CreateAddress);
    while (--count > 0) {
        if (count == 1)
            createAddressRequest.setHeaderInt(Api::RequestId, 1);
        m_connection.send(createAddressRequest);
    }

    m_pool.reserve(50);
    Streaming::MessageBuilder builder(m_pool);
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
    QMutexLocker lock(&m_walletMutex);
    m_wallet.saveKeys();
}

void TxVulcano::incomingMessage(const Message& message)
{
    // logDebug() << message.serviceId() << message.messageId() << message.body().size();
    if (message.serviceId() == Api::APIService && message.messageId() == Api::Meta::CommandFailed) {
        Streaming::MessageParser parser(message.body());
        int serviceId = -1;
        int messageId = -1;
        // std::string errorMessage;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::Meta::FailedCommandServiceId)
                serviceId = parser.intData();
            else if (parser.tag() == Api::Meta::FailedCommandId)
                messageId = parser.intData();
            // else if (parser.tag() == Api::Failures::FailedReason)
            //     errorMessage = parser.stringData();
        }
        if (serviceId == Api::LiveTransactionService && messageId == Api::LiveTransactions::SendTransaction) {
            int requestId = message.headerInt(Api::RequestId);
            QMutexLocker lock2(&m_miscMutex);
            auto iter = m_transactionsInProgress.find(requestId);
            if (iter != m_transactionsInProgress.end())
                m_transactionsInProgress.erase(iter);
        }
        // logDebug().nospace()
        //     << "incoming message recived a '" << errorMessage  << "` notification. S/C: " << serviceId << "/" << messageId;
    }
    else if (message.serviceId() == Api::UtilService && message.messageId() == Api::Util::CreateAddressReply) {
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::Util::PrivateKey) {
                CKey key;
                auto constBuf = parser.bytesDataBuffer();
                key.Set(reinterpret_cast<const unsigned char*>(constBuf.begin()),
                        reinterpret_cast<const unsigned char*>(constBuf.end()), true);

                if (key.IsValid()) {
                    QMutexLocker lock(&m_walletMutex);
                    m_wallet.addKey(key);
                    if (message.headerInt(Api::RequestId) == 1) { // the last one
                        m_wallet.saveKeys();
                    }
                } else  {
                    logCritical() << "Private address doesn't validate";
                }
            }
        }
    }
    else if (message.serviceId() == Api::BlockChainService && message.messageId() == Api::BlockChain::GetBlockHeaderReply) {
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::BlockHeight) {
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
            QCoreApplication::exit(1);
            return;
        }
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::BlockHeight) {
                m_highestBlock = parser.intData();
                QMutexLocker lock(&m_walletMutex);
                requestNextBlocksChunk();
                break;
            }
        }
        if (m_highestBlock == m_lastSeenBlock)
            nowCurrent();
        else if (m_lastSeenBlock > m_highestBlock) {
            logFatal() << "Hub went backwards in time...";
            QCoreApplication::exit(1);
        }
    }
    else if (message.serviceId() == Api::BlockChainService && message.messageId() == Api::BlockChain::GetBlockReply) {
        // this can take a lot of time to process, so process it on a different thread.
        emit newBlockFound(message);
    }
    else if (message.serviceId() == Api::RegTestService && message.messageId() == Api::RegTest::GenerateBlockReply) {
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::RegTest::BlockHash)
                logInfo() << "  Generate returns with a block hash: " << parser.uint256Data();
        }
        if (m_blockSizeLeft < 1000) {
            if (m_nextBlockSize.isEmpty())
                m_blockSizeLeft = 50000000;
            else
                m_blockSizeLeft = m_nextBlockSize.takeFirst() * 1000000;
            logCritical() << "Setting block size wanted to" << (m_blockSizeLeft / 1000000) << "MB";
            m_lastPrintedBlockSizeLeft = m_blockSizeLeft;
        }
    }
    else if (message.serviceId() == Api::BlockNotificationService && message.messageId() == Api::BlockNotification::NewBlockOnChain) {
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockNotification::BlockHash) {
                logInfo() << "Hub mined or found a new block:" << parser.uint256Data();
                QMutexLocker lock(&m_walletMutex);
                m_pool.reserve(40 + m_wallet.publicKeys().size() * 35);
                Streaming::MessageBuilder builder(m_pool);
                builder.add(Api::BlockChain::BlockHash, parser.uint256Data());
                bool first = true;
                buildGetBlockRequest(builder, first);
                m_connection.send(builder.message(Api::BlockChainService, Api::BlockChain::GetBlock));
            }
            else if (parser.tag() == Api::BlockNotification::BlockHeight) {
                m_highestBlock = std::max(m_highestBlock, parser.intData());
            }
        }
    }
    else if (message.serviceId() == Api::LiveTransactionService && message.messageId() == Api::LiveTransactions::SendTransactionReply) {
        QMutexLocker lock(&m_walletMutex);
        QMutexLocker lock2(&m_miscMutex);
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
                    assert(int(txData.pubKeys.size()) > outIndex);
                    m_wallet.addOutput(hash, outIndex, amount, txData.pubKeys.at(outIndex),
                                       txData.unconfirmedDepth + 1, script);
                    ++outIndex;
                }
            }
            m_transactionsInProgress.erase(item);
            if (++m_transactionsCreated > m_transactionsToCreate && m_transactionsToCreate > 0) {
                m_timer.cancel();
                logCritical() << "We created" << m_transactionsCreated << "transactions, completing the run & shutting down";
                generate(1);
                m_connection.disconnect();
                QCoreApplication::exit(0);
            }
            m_blockSizeLeft -= txData.transaction.size();
            if (m_lastPrintedBlockSizeLeft - m_blockSizeLeft > 10000000) {
                m_lastPrintedBlockSizeLeft = m_blockSizeLeft;
                logCritical() << "Block still"
                    << (m_lastPrintedBlockSizeLeft + 500000) / 1000000 << "MB from goal";
            }
            if (m_blockSizeLeft <= 0) {
                if (m_canRunGenerate)
                    logCritical() << "Block is full enough, calling generate()";
                else
                    logCritical() << "Block is full enough, waiting for miner to mine";
                m_transactionsInProgress.clear();
                m_wallet.clearUnconfirmedUTXOs();
                generate(1);
            }
        }
    }
    else if (message.serviceId() == Api::APIService && message.messageId() == Api::Meta::VersionReply) {
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::GenericByteData) {
                // Don't send the async header to older clients, they don't like it.
                m_serverSupportsAsync = parser.stringData().compare("Flowee:1 (2020-07)") >= 0;
            }
        }
    }
    else {
        Streaming::MessageParser::debugMessage(message);
    }
}

void TxVulcano::requestNextBlocksChunk()
{
    bool first = true;
    const auto ids = m_wallet.publicKeys();
    Q_ASSERT(!ids.empty()); // we should have received some directly after connecting.
    const int max = std::min(m_lastSeenBlock + 1000, m_highestBlock);
    m_pool.reserve((max - m_lastSeenBlock) * 15 + ids.size() * 36);
    Streaming::MessageBuilder builder(m_pool);
    for (int i = m_lastSeenBlock + 1; i <= max; ++i) {
        builder.add(Api::BlockChain::BlockHeight, i);
        buildGetBlockRequest(builder, first);
        auto m = builder.message(Api::BlockChainService, Api::BlockChain::GetBlock);
        if (i == max && max != m_highestBlock)
            m.setHeaderInt(LastBlockInChunk, 1);
        m_connection.send(m);
    }
}

void TxVulcano::processNewBlock(const Message &message)
{
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
    QMutexLocker lock(&m_walletMutex);
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::BlockChain::BlockHeight) {
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
        } else if (parser.tag() == Api::BlockChain::Tx_OutputScript) {
            auto constBuf = parser.bytesDataBuffer();
            script = CScript(reinterpret_cast<const unsigned char*>(constBuf.begin()),
                    reinterpret_cast<const unsigned char*>(constBuf.end()));
        } else if (parser.tag() == Api::BlockChain::Tx_Out_Index) {
            outIndex = parser.intData();
        } else if (parser.tag() == Api::BlockChain::Tx_Out_Address) {
            address = base_blob<160>(parser.bytesDataBuffer().begin());
            if (txOffsetInBlock > 0) {
                logDebug() << "Got Transaction in" << m_lastSeenBlock
                              << "@" << txOffsetInBlock
                              << "for" << amount
                              << "txid:" << txid
                              << "for address" << address;
                m_wallet.addOutput(m_lastSeenBlock, txid, txOffsetInBlock, outIndex, amount, address, script);
            }
        }
    }
    if (m_lastSeenBlock == m_highestBlock) {
        logInfo() << "Processed block" << m_highestBlock << "to find UTXOs";
        m_connection.postOnStrand(std::bind(&TxVulcano::nowCurrent, this));
    }
    if (message.headerInt(LastBlockInChunk) == 1) {
        logCritical() << "Processed up to block" << m_lastSeenBlock << "/" << m_highestBlock;
        // lets ask for the next blocks-chunk
        requestNextBlocksChunk();
    }
    else if (m_lastSeenBlock > 16000 && (m_lastSeenBlock % 100 == 0)) {
        // only really useful to log for scalenet.
        logInfo() << "Processed up to block" << m_lastSeenBlock << "/" << m_highestBlock;
    }
}

void TxVulcano::createTransactions(const boost::system::error_code& error)
{
    if (error)
        return;
    QMutexLocker lock(&m_miscMutex);
    if (m_transactionsInProgress.size() > 50) {
        // too many in flight, delay
        m_timer.expires_from_now(boost::posix_time::milliseconds(200));
        m_timer.async_wait(std::bind(&TxVulcano::createTransactions, this, std::placeholders::_1));
    } else {
        QTimer::singleShot(0, this, SLOT(createTransactions_priv()));
    }
}

void TxVulcano::createTransactions_priv()
{
    /*
     * TODO
     * The storage of unconfirmed UTXOs in the wallet is a bad idea. Lots of
     * overhead there for this one usecase.
     * What about I add a list of those in this class?
     */

    TransactionBuilder builder;
    short unconfirmedDepth = 0;
    int64_t amount = 0;
    QMutexLocker lock(&m_walletMutex);
    for (auto utxo = m_wallet.unspentOutputs().begin(); utxo != m_wallet.unspentOutputs().end();) {
        if (utxo->coinbaseHeight > 0 && utxo->coinbaseHeight + 99 > m_highestBlock) {// coinbase maturity
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
        builder.pushInputSignature(*key, utxo->prevOutScript, utxo->amount, TransactionBuilder::Schnorr);
        utxo = m_wallet.spendOutput(utxo);
        unconfirmedDepth = std::max(unconfirmedDepth, utxo->unconfirmedDepth);
        if (amount > 12500)
            break;
    }
    if (amount < 10000) {
        logCritical() << "No matured coins available.";
        m_timer.cancel();
        m_timer.expires_from_now(boost::posix_time::seconds(1));
        if (m_outOfCoin) {
            if (m_canRunGenerate) {
                logCritical() << " Calling generate";;
                m_timer.async_wait(std::bind(&TxVulcano::generate, this, 1));
            } else {
                logCritical() << " Waiting for a block to be mined";
            }
            return;
        }
        m_outOfCoin = true;
        logCritical() << " Slowing down";
        // try again with 1s delay;
        m_timer.async_wait(std::bind(&TxVulcano::createTransactions_priv, this));
        return;
    }
    else {
        m_outOfCoin = false;
    }

    const int OutputCount = m_wallet.unspentOutputs().size() < 5000 ? 20 :
                            m_wallet.unspentOutputs().size() < 20000 ? 10 : 2;
    const int64_t outAmount = (amount - MIN_FEE - 100 * OutputCount) / OutputCount;

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
        builder.pushOutputPay2Address(m_wallet.publicKey(out).getKeyId());
        unvalidatedTransaction.pubKeys.push_back(out);
    }
    assert(count > 0);

    m_Txpool.reserve(1000); // Should be plenty
    Tx signedTx = builder.createTransaction(&m_Txpool);

    m_Txpool.reserve(signedTx.size() + 30);
    Streaming::MessageBuilder mb(m_Txpool);
    mb.add(Api::LiveTransactions::Transaction, signedTx.data());
    Message m(mb.message(Api::LiveTransactionService, Api::LiveTransactions::SendTransaction));
    if (m_serverSupportsAsync)
        m.setHeaderInt(Api::ASyncRequest, true);

    unvalidatedTransaction.transaction = signedTx;
    {
        QMutexLocker lock(&m_miscMutex);
        const int id = ++m_lastId;
        m_transactionsInProgress.insert(std::make_pair(id, unvalidatedTransaction));
        m.setHeaderInt(Api::RequestId, id);
    }
    m_connection.send(m);

    // wait until next eventloop so we still do network in the meantime
    m_timer.cancel();
    m_timer.expires_from_now(boost::posix_time::milliseconds(0));
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
            const CKeyID id = m_wallet.publicKey(i).getKeyId();
            CashAddress::Content c { CashAddress::PUBKEY_TYPE, std::vector<uint8_t>(id.begin(), id.end()) };
            builder.add(first ? Api::BlockChain::SetFilterScriptHash : Api::BlockChain::AddFilterScriptHash,
                        CashAddress::createHashedOutputScript(c));
            first = false;
        }
    } else {
        builder.add(Api::BlockChain::ReuseAddressFilter, true);
    }

    builder.add(Api::BlockChain::Include_TxId, true);
    builder.add(Api::BlockChain::Include_OffsetInBlock, true);
    builder.add(Api::BlockChain::Include_OutputAmounts, true);
    builder.add(Api::BlockChain::Include_OutputAddresses, true);
    builder.add(Api::BlockChain::Include_OutputScripts, true);
}

void TxVulcano::nowCurrent()
{
    if (m_canRunGenerate && m_wallet.unspentOutputs().size() < 10)
        generate(110);
    else
        QTimer::singleShot(0, this, SLOT(createTransactions_priv()));
}

void TxVulcano::generate(int blockCount)
{
    if (!m_canRunGenerate)
        return;
    m_pool.reserve(30);
    Streaming::MessageBuilder builder(m_pool);
    QMutexLocker lock(&m_walletMutex);
    int pkId = m_wallet.firstEmptyPubKey();
    assert(pkId >= 0);
    const CKeyID id = m_wallet.publicKey(pkId).getKeyId();
    builder.addByteArray(Api::RegTest::BitcoinP2PKHAddress, id.begin(), id.size());
    builder.add(Api::RegTest::Amount, blockCount);
    auto log = logCritical() << "  Sending generate";
    if (m_blockSizeLeft >= 1000)
        log << "The block size we aimed for is still" << (m_blockSizeLeft / 1000) << "KB away";
    m_connection.send(builder.message(Api::RegTestService, Api::RegTest::GenerateBlock));
}

bool TxVulcano::canRunGenerate() const
{
    return m_canRunGenerate;
}

void TxVulcano::setCanRunGenerate(bool canRunGenerate)
{
    m_canRunGenerate = canRunGenerate;
}

bool TxVulcano::addPrivKey(const QString &key)
{
    CBase58Data encoded;
    encoded.SetString(key.toStdString());
    if (encoded.isMainnetPrivKey()) {
        logFatal() << "Priv key is mainnet, not acceptable!";
        return false;
    }
    if (!encoded.isTestnetPrivKey()) {
        logFatal() << "Priv key did not parse. Please provide a WIF encoded priv key.";
        logCritical() << "Example: cQuN3nAuS4VZNscSJqzBQSWzLix1SEfCtxitMsDyS5Mz7ddAXvMo";
        return false;
    }
    QMutexLocker lock(&m_walletMutex);
    CKey privKey;
    const auto &data = encoded.data();
    assert(data.size() >= 32);
    privKey.Set(data.begin(), data.begin() + 32, data.size() > 32 && data[32] == 1);
    if (!privKey.IsValid()) {
        logFatal() << "Private key did not validate";
        return false;
    }
    m_wallet.addKey(privKey);
    return true;
}
