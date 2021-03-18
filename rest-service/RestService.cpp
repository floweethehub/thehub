/*
 * This file is part of the Flowee project
 * Copyright (C) 2019-2021 Tom Zander <tom@flowee.org>
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
#include "RestService.h"
#include <Blockchain_p.h>
#include <httpengine/socket.h>
#include <primitives/script.h>
#include <primitives/FastTransaction.h>
#include <primitives/key.h>
#include <Streaming.h>

#include <utilstrencodings.h>
#include <base58.h>
#include <cashaddr.h>

#include <QSettings>
#include <QJsonArray>
#include <QThread>
#include <QFile>
#include <QTimer>

enum Base58Type {
    PUBKEY_ADDRESS,
    SCRIPT_ADDRESS
};

static QJsonDocument::JsonFormat s_JsonFormat = QJsonDocument::Compact;
static QString s_servicePrefixPath = QString("/v2/");

class UserInputException : public std::runtime_error
{
public:
    explicit UserInputException(const char *error) : std::runtime_error(error) { }
};


namespace {

/* Address-lookup data */
struct TransactionId {
    TransactionId(int height, int oib) : blockHeight(height), offsetInBlock(oib) {}
    int blockHeight = -1;
    int offsetInBlock = 0;

    bool operator==(const TransactionId &o) const  {
        return o.blockHeight == blockHeight
                && o.offsetInBlock == offsetInBlock;
    }
    bool operator<(const TransactionId &o) const {
        if (blockHeight == o.blockHeight)
            return offsetInBlock < o.offsetInBlock;
        if (blockHeight == -1) // -1 is actually the mempool, so newer than the highest block
            return false;
        if (o.blockHeight == -1)
            return true;
        return blockHeight < o.blockHeight;
    }
};
struct UTXOEntry {
    UTXOEntry(int height, int oib, int index)
        : blockHeight(height), offsetInBlock(oib), outIndex(index) {}
    UTXOEntry(int height, int oib, int index, qint64 amount_)
        : blockHeight(height), offsetInBlock(oib), outIndex(index), amount(amount_) {}
    int blockHeight = -1;
    int offsetInBlock = 0;
    int outIndex = -1;
    qint64 amount = -1;

    bool unspent = true;
};

quint64 qHash(const TransactionId &utxo) {
    quint64 answer = static_cast<quint64>(utxo.blockHeight) << 44;
    answer += utxo.offsetInBlock;
    return answer;
}
struct AnswerListingDataSingle {
    AnswerListingDataSingle(const CashAddress::Content &a)
        : address(a)
    {
    }

    CashAddress::Content address;
    std::vector<UTXOEntry> utxos;
    QSet<TransactionId> fetchedTransactions;
    std::list<int> transactions; // transactions belonging to this answer, index in 'answer'

};

struct AddressListingData : public AnswerDataBase {
    QList<AnswerListingDataSingle> items;
    int m_cur = 0;

    AnswerListingDataSingle *cur() {
        assert(m_cur <= items.size());
        return &items[m_cur];
    }

    // extract a list of addresses from the json
    void fromJson(const QJsonDocument &doc) {
        auto in = doc.object()["addresses"];
        if (!in.isArray())
            throw UserInputException("Input invalid");
        auto array = in.toArray();
        for (auto i = array.begin(); i != array.end(); ++i) {
            if (!i->isString())
                throw UserInputException("Input invalid");
            addAddress(i->toString().toStdString());
        }
    }

    void fromSingleAddress(const QString &address) {
        addAddress(address.toStdString());
    }

private:
    void addAddress(const std::string &address) {
        CashAddress::Content c;
        CBase58Data old; // legacy address encoding
        if (old.SetString(address)) {
            c.hash = old.data();
            if (old.isMainnetPkh()) {
                c.type = CashAddress::PUBKEY_TYPE;
            } else if (old.isMainnetSh()) {
                c.type = CashAddress::SCRIPT_TYPE;
            } else {
                throw UserInputException("Invalid (legacy) address type");
            }
        }
        else {
            // TODO make configurable to show testnet addresses for testnet servers
            c = CashAddress::decodeCashAddrContent(address, "bitcoincash");
        }
        if (c.hash.size() != 20)
            throw UserInputException("Invalid address");
        items.append(AnswerListingDataSingle(c));
    }
};

// TODO make configurable to show different data for testnet.
static QString ripeToLegacyAddress(const std::vector<quint8> &in, CashAddress::AddressType type)
{
    CBase58Data answer;
    CKeyID id(reinterpret_cast<const char *>(in.data()));
    switch (type) {
    case CashAddress::PUBKEY_TYPE:
        answer.setData(id, CBase58Data::PubkeyType, CBase58Data::Mainnet);
        break;
    case CashAddress::SCRIPT_TYPE:
        answer.setData(id, CBase58Data::ScriptType, CBase58Data::Mainnet);
        break;
    default:
        assert(false);
        return QString();
    }
    return QString::fromStdString(answer.ToString());
}

static QString ripeToCashAddress(const std::vector<quint8> &in, CashAddress::AddressType type)
{
    // TODO make configurable to show testnet addresses for testnet servers
    return QString::fromStdString(CashAddress::encodeCashAddr("bitcoincash", { type, in }));
}

QString satoshisToBCH(quint64 sats)
{
    constexpr quint64 COIN = 100000000;
    // the format is to always have 8 digits behind the dot.
    QString answer = QString("%1.00000000").arg(sats / COIN);
    sats = sats % COIN;
    QString change = QString::number(sats);

    return answer.left(answer.length() - change.length()) + change;
}

Streaming::ConstBuffer hexStringToBuffer(const QString &hash, Streaming::BufferPool &pool)
{
    if (hash.length() / 2 * 2 != hash.length())
        throw std::runtime_error("invalid sized hash, odd number of chars");
    pool.reserve(hash.length() / 2);

    char *buf = pool.begin();
    for (int i = 0; i < hash.length(); ++i) {
        QChar k = hash.at(i);
        uint8_t v = static_cast<uint8_t>(HexDigit(static_cast<int8_t>(k.unicode())));
        if (k.unicode() > 'f' || v == 0xFF)
            throw std::runtime_error("Not a hash");
        if ((i % 2) == 0) {
            *buf = static_cast<char>(v << 4);
        } else {
            *buf += v;
            ++buf;
        }
    }
    return pool.commit(hash.length() / 2);
}

// The uint256 serialization for some reason reverses the ordering.
Streaming::ConstBuffer uint256StringToBuffer(const QString &hash, Streaming::BufferPool &pool)
{
    if (hash.size() < 64)
        throw std::runtime_error("invalid sized hash" );

    pool.reserve(32);
    int i2 = 31;
    for (int i = 0; i < 64; ++i) {
        QChar k = hash.at(i);
        uint8_t v = static_cast<uint8_t>(HexDigit(static_cast<int8_t>(k.unicode())));
        if (k.unicode() > 'f' || v == 0xFF)
            throw std::runtime_error("Not a hash");
        if ((i % 2) == 0) {
            pool.begin()[i2] = static_cast<char>(v << 4);
        } else {
            pool.begin()[i2--] += v;
        }
    }
    return pool.commit(32);
}

QChar hexChar(uint8_t k) {
    assert(k < 16);
    if (k < 10)
        return QChar('0' + k);
    return QChar('a' + k - 10);
}

static const char hexmap[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
                                 '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

void writeAsHexString(const Streaming::ConstBuffer &buf, QIODevice *device)
{
    for (const char *k = buf.begin(); k != buf.end(); ++k) {
        uint8_t val = static_cast<uint8_t>(*k);
        const char byte[2] = {
            hexmap[val >> 4],
            hexmap[val & 15],
        };
        device->write(byte, 2);
    }
}

void writeAsHexStringReversed(const Streaming::ConstBuffer &buf, QIODevice *device)
{
    const char *k = buf.end();
    do {
        --k;
        uint8_t val = static_cast<uint8_t>(*k);
        const char byte[2] = {
            hexmap[val >> 4],
            hexmap[val & 15],
        };
        device->write(byte, 2);
    } while (k != buf.begin());
}

QString uint256ToString(const Streaming::ConstBuffer &buf)
{
    assert(buf.size() == 32);
    QString answer;
    answer.resize(64, QChar('0'));
    QChar *string = answer.data();

    for (int pos = buf.size() - 1; pos >= 0; --pos) {
        uint8_t k = static_cast<uint8_t>(buf.begin()[pos]);
        *string++ = hexChar((k >> 4) & 0xF).unicode();
        *string++ = hexChar(k & 0xF).unicode();
    }
    return answer;
}

QJsonObject toJson(const Blockchain::BlockHeader &header, QJsonObject &answer)
{
    answer.insert("blocktime", qint64(header.time));
    answer.insert("time", qint64(header.time));
    answer.insert("confirmations", header.confirmations);
    if (!answer.contains("blockhash"))
        answer.insert("blockhash", uint256ToString(header.hash));

    return answer;
}

QString parseOutScriptAddAddresses(QJsonArray &addresses, QJsonArray &cashAddresses, const Streaming::ConstBuffer &script)
{
    CScript scriptPubKey(script);

    std::vector<std::vector<unsigned char> > vSolutions;
    Script::TxnOutType whichType;
    bool recognizedTx = Script::solver(scriptPubKey, whichType, vSolutions);
    QString type; // TODO what is the output for unrecognized?
    if (recognizedTx && (whichType == Script::TX_PUBKEY
                         || whichType == Script::TX_PUBKEYHASH || whichType == Script::TX_SCRIPTHASH)) {
        if (whichType == Script::TX_SCRIPTHASH) {
            Q_ASSERT(vSolutions[0].size() == 20);

            addresses.append(ripeToLegacyAddress(vSolutions[0], CashAddress::SCRIPT_TYPE));
            cashAddresses.append(ripeToCashAddress(vSolutions[0], CashAddress::SCRIPT_TYPE));
            type = "scripthash";
        } else if (whichType == Script::TX_PUBKEYHASH) {
            Q_ASSERT(vSolutions[0].size() == 20);
            addresses.append(ripeToLegacyAddress(vSolutions[0], CashAddress::PUBKEY_TYPE));
            cashAddresses.append(ripeToCashAddress(vSolutions[0], CashAddress::PUBKEY_TYPE));
            type = "pubkeyhash";
        } else if (whichType == Script::TX_PUBKEY) {
            CPubKey pubKey(vSolutions[0]);
            Q_ASSERT (pubKey.IsValid());
            CKeyID address = pubKey.GetID();
            std::vector<quint8> id(address.begin(), address.end());
            addresses.append(ripeToLegacyAddress(id, CashAddress::PUBKEY_TYPE));
            cashAddresses.append(ripeToCashAddress(id, CashAddress::PUBKEY_TYPE));
            type = "pubkey"; // TODO verify this is what bitcoin,com used
        }
    }

    // TODO multisig
    return type;
}
}

RestService::RestService()
{
}

void RestService::onIncomingConnection(HttpEngine::WebRequest *request_)
{
    Q_ASSERT(request_);
    auto request = qobject_cast<RestServiceWebRequest*>(request_);
    Q_ASSERT(request);
    auto socket = request->socket();
    if (socket->method() == HttpEngine::Socket::POST) {
        if (socket->contentLength() > 250000) {
            // POST data exceeds maximum, just close
            socket->close();
            return;
        }
        if (socket->contentLength() > socket->bytesAvailable()) {
            // wait for the full POST data to become available.
            QObject::connect(socket, &HttpEngine::Socket::readChannelFinished, [this, request]() {
                this->onIncomingConnection(request);
            });
            return;
        }
    }
    QObject::connect(socket, SIGNAL(disconnected()), request, SLOT(deleteLater()));

    if (socket->method() != HttpEngine::Socket::HEAD
            && socket->method() != HttpEngine::Socket::GET
            && socket->method() != HttpEngine::Socket::POST) {
        socket->close();
        return;
    }
    socket->setHeader("server", "Flowee");
    RequestString rs(socket->path());
    if (rs.wholePath.isEmpty() || rs.request.isEmpty()) {
        returnTemplatePath(socket, "index.html");
        return;
    }
    const QString now = QString("%1 GMT").arg(QDateTime::currentDateTimeUtc().toString("ddd, d MMM yyyy h:mm:ss"));
    socket->setHeader("last-modified", now.toLatin1()); // no cashing.
    if (socket->method() == HttpEngine::Socket::HEAD) {
        socket->writeHeaders();
        socket->close();
        return;
    }
    logWarning().nospace() << (socket->method() == HttpEngine::Socket::GET ? "GET\t" : "POST\t")
                           << socket->peerAddress().toString() << "\t" << rs.anonPath()
                           << "\t" << socket->headers().value("User-Agent").data();

    if (request->socket()->method() == HttpEngine::Socket::POST
            && !request->socket()->readJson(rs.post)) {
        logWarning() << "Unparsable JSON in POST request";
        return;
    }
    static QString errorJson = "error.json";
    try {
        QString errorPage = errorJson;
        if (rs.request == "transaction/details") {
            requestTransactionInfo(rs, request);
        } else if (rs.request.startsWith("address")) {
            requestAddressInfo(rs, request);
        } else if (rs.request.startsWith("rawtransactions")) {
            requestRawTransaction(rs, request);
        } else if (rs.request.startsWith("help/")) {
            if (rs.request == "help/transaction")
                errorPage = "txHelp.html";
            else if (rs.request == "help/address")
                errorPage = "addressHelp.html";
            else if (rs.request == "help/rawtransaction")
                errorPage = "rawTxHelp.html";
        }

        if (request->answerType)
            start(request);
        else
            returnTemplatePath(socket, errorPage);
    } catch (const Blockchain::ServiceUnavailableException &e) {
        request->aborted(e);
    } catch (const UserInputException &e) {
        returnTemplatePath(socket, errorJson, e.what());
    } catch (const std::exception &e) {
        logCritical() << "Failed to handle request because of" << e;
        socket->writeError(HttpEngine::Socket::ServiceUnavailable);
    }
}

void RestService::parseConfig(const std::string &confFile)
{
    QSettings conf(QString::fromStdString(confFile), QSettings::IniFormat);
    s_servicePrefixPath = conf.value("url.prefix", "/v2/").toString().trimmed();
    if (!s_servicePrefixPath.startsWith("/"))
        s_servicePrefixPath = "/" + s_servicePrefixPath;
    if (!s_servicePrefixPath.endsWith("/"))
        s_servicePrefixPath += "/";
    conf.beginGroup("json");
    s_JsonFormat = conf.value("compact", true).toBool() ? QJsonDocument::Compact : QJsonDocument::Indented;

    conf.endGroup();
}

void RestService::initializeHubConnection(NetworkConnection con, const std::string &)
{
    con.send(Message(Api::BlockChainService, Api::BlockChain::GetBlockCount));
    con.send(Message(Api::BlockNotificationService, Api::BlockNotification::Subscribe));
}

void RestService::onReparseConfig()
{
    reparseConfig();
}

void RestService::requestTransactionInfo(const RequestString &rs, RestServiceWebRequest *request)
{
    if (!rs.argument.isEmpty()) {
        Blockchain::Job job;
        job.type = Blockchain::FetchTx;
        try {
            job.data = uint256StringToBuffer(rs.argument, d->pool(32));
        } catch (const std::runtime_error &e) {
            throw UserInputException(e.what());
        }
        job.transactionFilters = Blockchain::IncludeFullTransactionData | Blockchain::IncludeTxFees;
        job.nextJobId = 1; // that would be the 'fetchBlockHeader'
        std::lock_guard<std::mutex> lock(request->jobsLock);
        request->jobs.push_back(job);
        job = Blockchain::Job();
        job.type = Blockchain::FetchBlockHeader;
        request->jobs.push_back(job);
        request->answerType = RestServiceWebRequest::TransactionDetails;
    }
    else if (rs.post.isObject()) {
        const auto inputJson = rs.post.object();
        auto txids = inputJson["txs"];
        if (!txids.isArray())
            throw UserInputException("Input invalid");
        auto array = txids.toArray();
        for (auto i = array.begin(); i != array.end(); ++i) {
            if (!i->isString())
                throw UserInputException("Input invalid");

            Blockchain::Job job;
            job.type = Blockchain::FetchTx;
            try {
                job.data = uint256StringToBuffer(i->toString(), d->pool(32));
            } catch (const std::runtime_error &e) {
                throw UserInputException(e.what());
            }
            job.transactionFilters = Blockchain::IncludeFullTransactionData | Blockchain::IncludeTxFees;
            job.nextJobId = request->jobs.size() + 1; // that would be the 'fetchBlockHeader'
            std::lock_guard<std::mutex> lock(request->jobsLock);
            request->jobs.push_back(job);
            job = Blockchain::Job();
            job.type = Blockchain::FetchBlockHeader;
            request->jobs.push_back(job);
            request->answerType = RestServiceWebRequest::TransactionDetailsList;
        }
    }
    else
        throw UserInputException("Endpoint not recognized, check for typos!");
}

void RestService::requestAddressInfo(const RequestString &rs, RestServiceWebRequest *request)
{
    if (rs.request == "address/details") {
        std::unique_ptr<AddressListingData>address(new AddressListingData());

        if (!rs.argument.isEmpty()) {
            address->fromSingleAddress(rs.argument);
            request->answerType = RestServiceWebRequest::AddressDetails;
        }
        else if (rs.post.isObject()) {
            address->fromJson(rs.post);
            request->answerType = RestServiceWebRequest::AddressDetailsList;
        }
        if (address->items.isEmpty())
            throw UserInputException("No input");

        std::lock_guard<std::mutex> lock(request->jobsLock);
        Blockchain::Job job;
        job.type = Blockchain::LookupByAddress;
        job.data = CashAddress::createHashedOutputScript(address->items.at(0).address);
        request->jobs.push_back(job);

        job.type = Blockchain::FindAddressInMempool;
        job.transactionFilters = Blockchain::IncludeOutputAmounts | Blockchain::IncludeTxId;
        request->jobs.push_back(job);
        request->answerData = address.release();
    }
    else if (rs.request == "address/utxo") {
        std::unique_ptr<AddressListingData>address(new AddressListingData());
        if (!rs.argument.isEmpty()) {
            address->fromSingleAddress(rs.argument);
            request->answerType = RestServiceWebRequest::AddressUTXO;
        }
        else if (rs.post.isObject()) {
            address->fromJson(rs.post);
            request->answerType = RestServiceWebRequest::AddressUTXOList;
        }
        if (address->items.isEmpty())
            throw UserInputException("No input");

        Blockchain::Job job;
        job.type = Blockchain::LookupByAddress;
        job.data = CashAddress::createHashedOutputScript(address->items.at(0).address);
        std::lock_guard<std::mutex> lock(request->jobsLock);
        request->answerType = RestServiceWebRequest::AddressUTXO;
        request->answerData = address.release();
        request->jobs.push_back(job);
    }
    else
        throw UserInputException("Endpoint not recognized, check for typos!");
}

void RestService::requestRawTransaction(const RequestString &rs, RestServiceWebRequest *request)
{
    if (rs.request == "rawtransactions/getRawTransaction") {
        if (!rs.argument.isEmpty()) {
            Blockchain::Job job;
            job.type = Blockchain::FetchTx;
            job.transactionFilters = Blockchain::IncludeFullTransactionData;
            try {
                job.data = uint256StringToBuffer(rs.argument, d->pool(32));
            } catch (const std::runtime_error &e) {
                throw UserInputException(e.what());
            }
            std::lock_guard<std::mutex> lock(request->jobsLock);
            request->answerType = RestServiceWebRequest::GetRawTransaction;
            auto args = request->socket()->queryString();
            qWarning() << args;
            auto verbose = args.find("verbose");
            if (verbose != args.end() && verbose.value().toLower() == "true") {
                job.nextJobId = 1; // that would be the 'fetchBlockHeader'
                request->jobs.push_back(job);
                job = Blockchain::Job();
                job.type = Blockchain::FetchBlockHeader;
                request->jobs.push_back(job);
                request->answerType = RestServiceWebRequest::GetRawTransactionVerbose;
            }
            request->jobs.push_back(job);
        }
        // TODO POST
        else
            throw UserInputException("POST no supported yet");
    }
    else if (rs.request == "rawtransactions/sendRawTransaction") {
        if (!rs.argument.isEmpty()) {
            Streaming::ConstBuffer tx;
            try {
                tx = hexStringToBuffer(rs.argument, d->pool(32));
            } catch (const std::runtime_error &e) {
                throw UserInputException(e.what());
            }
            if (tx.size() <= 60 || tx.size() == 64)
                throw UserInputException("Tx too small");
            if (tx.size() > 100000)
                throw UserInputException("Tx too large");

            Streaming::MessageBuilder builder(d->pool(tx.size() + 5));
            builder.add(Api::GenericByteData, tx);

            Blockchain::Job job;
            job.data = builder.buffer();
            job.intData = Api::LiveTransactionService;
            job.intData2 = Api::LiveTransactions::SendTransaction;
            job.type = Blockchain::CustomHubMessage;
            request->jobs.push_back(job);
            request->answerType = RestServiceWebRequest::SendRawTransaction;
        }
        else if (rs.post.isObject()) {
            const auto inputJson = rs.post.object();
            auto hexes = inputJson["hexes"];
            if (!hexes.isArray())
                throw UserInputException("Input invalid");
            auto array = hexes.toArray();
            for (auto i = array.begin(); i != array.end(); ++i) {
                if (!i->isString())
                    throw UserInputException("Input invalid");
                Streaming::ConstBuffer tx;
                try {
                    tx = hexStringToBuffer(i->toString(), d->pool(32));
                } catch (const std::runtime_error &e) {
                    throw UserInputException(e.what());
                }
                if (tx.size() <= 60 || tx.size() == 64)
                    throw UserInputException("Tx too small");
                if (tx.size() > 100000)
                    throw UserInputException("Tx too large");

                Streaming::MessageBuilder builder(d->pool(tx.size() + 5));
                builder.add(Api::GenericByteData, tx);

                Blockchain::Job job;
                job.data = builder.buffer();
                job.intData = Api::LiveTransactionService;
                job.intData2 = Api::LiveTransactions::SendTransaction;
                job.type = Blockchain::CustomHubMessage;
                request->jobs.push_back(job);
                request->answerType = RestServiceWebRequest::SendRawTransaction;
            }
        }
    }
    else
        throw UserInputException("Endpoint not recognized, check for typos!");
}

// ------------------------------------------

RequestString::RequestString(const QString &path)
{
    if (path.startsWith(s_servicePrefixPath)) {
        wholePath = path;
        int seprator = path.indexOf("/", s_servicePrefixPath.size());
        if (seprator == -1) // then to the end of the line.
            seprator = path.length() - 1;

        int slash2 = path.indexOf("/", seprator + 1);
        if (slash2 == -1) {
            // something like /address/utxo
            request = path.mid(s_servicePrefixPath.size());
        } else {
            // something like /address/utxo/{address}
            request = path.mid(s_servicePrefixPath.size(), slash2 - s_servicePrefixPath.size());
            argument = path.mid(slash2+1);
        }
    }
}

QString RequestString::anonPath() const
{
    if (argument.isEmpty())
        return request;
    return QString("%1/{ARG}").arg(request);
}

RestServiceWebRequest::RestServiceWebRequest(qintptr socketDescriptor, std::function<void (HttpEngine::WebRequest *)> &handler)
    : HttpEngine::WebRequest(socketDescriptor, handler)
{
}

RestServiceWebRequest::~RestServiceWebRequest()
{
    delete answerData;
}

void RestServiceWebRequest::finished(int unfinishedJobs)
{
    Q_UNUSED(unfinishedJobs)
    // SearchEngine does everything in the threads that it uses for individual connections.
    // Our http engine wants to use its own thread, so lets move threads.
    if (answerType == TransactionDetails && answer.empty() && jobs.size() <= 4) {
        Blockchain::Job job;
        job.type = Blockchain::FindTxInMempool;
        job.transactionFilters = Blockchain::IncludeFullTransactionData;
        job.data = jobs[0].data;
        {
            std::lock_guard<std::mutex> lock(jobsLock);
            jobs.push_back(job);
        }
        policy->processRequests(this);
        return;
    }
    auto *ald = dynamic_cast<AddressListingData*>(answerData);
    if (ald) {
        if (ald->items.size() -1 > ald->m_cur) { // not finished yet!
            Blockchain::Job job;
            // we go and run the next one in the list.
            ald->m_cur++;
            job.type = Blockchain::LookupByAddress;
            job.data = CashAddress::createHashedOutputScript(ald->cur()->address);
            jobs.push_back(job);
            if (answerType == AddressDetailsList) {
                job.type = Blockchain::FindAddressInMempool;
                job.transactionFilters = Blockchain::IncludeOutputAmounts | Blockchain::IncludeTxId;
                jobs.push_back(job);
            }
            policy->processRequests(this);
            return;
        }
    }

    QTimer::singleShot(0, this, SLOT(threadSafeFinished()));
}

void RestServiceWebRequest::transactionAdded(const Blockchain::Transaction &transaction, int answerIndex)
{
    logDebug() << "Fetched Tx:" << transaction.blockHeight << transaction.offsetInBlock << transaction.jobId;
    if (!transaction.txid.isEmpty())
        logDebug() << "          :" << uint256ToString(transaction.txid);
    if ((answerType == TransactionDetails || answerType == TransactionDetailsList)
            && !transaction.fullTxData.isEmpty()) { // if (one of) the main transaction

        // For each input we want to know the value, so we need to do two lookups for that (spent-db and outputvalue)
        // For each output we want to know who spent it.
        Tx::Iterator iter(transaction.fullTxData);
        Blockchain::Job job;
        int inputIndex = 0;
        int outputIndex = 0;
        while (iter.next() != Tx::End) {
            switch (iter.tag()) {
            case Tx::PrevTxHash:
                job = Blockchain::Job();
                job.data = iter.byteData();
                break;
            case Tx::PrevTxIndex:
                if (!transaction.isCoinbase()) {
                    job.type = Blockchain::LookupTxById;
                    job.nextJobId = jobs.size() + 2;
                    jobs.push_back(job);

                    // fetch both the mempool one and the blockchain one at the same time to ensure we get an answer.
                    Blockchain::Job memJob;
                    memJob.data = job.data;
                    memJob.type = Blockchain::FindTxInMempool;
                    memJob.transactionFilters = Blockchain::IncludeOutputAmounts
                            + Blockchain::IncludeOutputScripts;
                    txRefs.insert(std::make_pair(jobs.size(), txRefKey(answerIndex, TxRef::Input, inputIndex)));
                    jobs.push_back(memJob);

                    job = Blockchain::Job();
                    job.type = Blockchain::FetchTx;
                    job.transactionFilters = Blockchain::IncludeOutputAmounts
                            + Blockchain::IncludeOutputScripts;
                    txRefs.insert(std::make_pair(jobs.size(), txRefKey(answerIndex, TxRef::Input, inputIndex++)));
                    jobs.push_back(job);
                }
                break;
            case Tx::OutputValue:
                job = Blockchain::Job();
                job.type = Blockchain::LookupSpentTx;
                job.intData = outputIndex;
                job.data = transaction.txid;
                job.nextJobId = jobs.size() + 1;
                jobs.push_back(job);

                if (transaction.blockHeight > 0) {
                    // only fetch from the blockchain if the transaction itself has been mined
                    txRefs.insert(std::make_pair(jobs.size(), txRefKey(answerIndex, TxRef::Output, outputIndex++)));
                    job = Blockchain::Job();
                    job.type = Blockchain::FetchTx;
                    job.transactionFilters = Blockchain::IncludeTxId;
                    jobs.push_back(job);
                }
                // TODO mempool?
                break;
            case Tx::End:
                return;
            default:
                break;
            }
        }
        return;
    }
    auto *ald = dynamic_cast<AddressListingData*>(answerData);
    if (ald && (answerType == AddressDetails || answerType == AddressDetailsList)) {
        auto cur = ald->cur();
        cur->transactions.push_back(answerIndex);
        if (transaction.blockHeight == -1) { // from mempool
            assert(transaction.outIndex != -1);
            assert(transaction.outputs.size() <= 0x7fff); // since we cast to short next
            assert(short(transaction.outputs.size()) > transaction.outIndex);
            ald->cur()->utxos.push_back(UTXOEntry(-1, -1, transaction.outIndex,
                           transaction.outputs[transaction.outIndex].amount));
            return;
        }
        // we receive a tx for our address search because it deposited an output to our target address.
        //
        //   - update the utxos to the amount it deposited
        //   - find out if it was spent and by whom. (new job)
        //
        // TODO replace with binary search (it uses blockheight highest to lowest)
        auto job = Blockchain::Job();
        size_t i = 0;
        for (;i < cur->utxos.size(); ++i) {
            auto &utxo = cur->utxos[i];
            if (utxo.blockHeight == transaction.blockHeight
                    && utxo.offsetInBlock == transaction.offsetInBlock) {

                job.intData = utxo.outIndex;
                utxo.amount = transaction.outputs.at(utxo.outIndex).amount;
                job.type = Blockchain::LookupSpentTx;
                job.data = transaction.txid;
                job.intData3 = i; // remember the utxo entry this is for
                jobs.push_back(job);
                return;
            }
        }
    }
}

void RestServiceWebRequest::spentOutputResolved(int jobId, int blockHeight, int offsetInBlock)
{
    if (blockHeight > 0) {// it was spent
        auto *ald = dynamic_cast<AddressListingData*>(answerData);
        if (ald && (answerType == AddressDetails || answerType == AddressDetailsList)) { // this is about listing an address
            // outputs that deposited something on our requestors address got checked if they were spent.
            // We now have to update the data structure and if they were spent we have to find out
            // the txid that spent it.

            const auto &origJob = jobs[jobId];
            auto utxoIndex = origJob.intData3;
            // TODO check bounds
            auto cur = ald->cur();
            cur->utxos[utxoIndex].unspent = false;
            if (!cur->fetchedTransactions.contains({blockHeight, offsetInBlock})) {
                cur->fetchedTransactions.insert({blockHeight, offsetInBlock});
                auto job = Blockchain::Job();
                job.intData = blockHeight;
                job.intData2 = offsetInBlock;
                job.type = Blockchain::FetchTx;
                job.transactionFilters = Blockchain::IncludeTxId;
                jobs.push_back(job);
            }
        }
    }
}

void RestServiceWebRequest::addressUsedInOutput(int blockHeight, int offsetInBlock, int outIndex)
{
    auto *ald = dynamic_cast<AddressListingData*>(answerData);
    if (ald) {
        if (answerType == AddressDetails || answerType == AddressDetailsList) {
            auto cur = ald->cur();
            cur->utxos.push_back({blockHeight, offsetInBlock, outIndex});
            if (!cur->fetchedTransactions.contains({blockHeight, offsetInBlock})) {
                cur->fetchedTransactions.insert({blockHeight, offsetInBlock});
                auto job = Blockchain::Job();
                job.intData = blockHeight;
                job.intData2 = offsetInBlock;
                job.type = Blockchain::FetchTx;
                job.transactionFilters = Blockchain::IncludeTxId | Blockchain::IncludeOutputAmounts;
                jobs.push_back(job);
            }
        } else if (answerType == AddressUTXO) {
            auto job = Blockchain::Job();
            job.intData = blockHeight;
            job.intData2 = offsetInBlock;
            job.intData3 = outIndex;
            job.type = Blockchain::FetchUTXODetails;
            jobs.push_back(job);
        }
    }
}

void RestServiceWebRequest::utxoLookup(int jobId, int blockHeight, int offsetInBlock, int outIndex, bool unspent, int64_t amount, Streaming::ConstBuffer)
{
    logDebug() << "utxo lookup returned for job" << jobId << blockHeight << offsetInBlock << unspent <<  amount;
    auto *ald = dynamic_cast<AddressListingData*>(answerData);
    if (ald && answerType == AddressUTXO) {
        if (unspent) { // we only care about unspent here.
            ald->cur()->utxos.push_back({blockHeight, offsetInBlock, outIndex, amount});

            auto job = Blockchain::Job();
            job.intData = blockHeight;
            job.intData2 = offsetInBlock;
            job.type = Blockchain::FetchTx;
            job.transactionFilters = Blockchain::IncludeTxId;
            jobs.push_back(job);
        }
    }
}

void RestServiceWebRequest::aborted(const Blockchain::ServiceUnavailableException &e)
{
    QString error("could not find upstream service: %1");
    switch (e.service()) {
    case Blockchain::TheHub:
        error = error.arg("The Hub");
        break;
    case Blockchain::IndexerTxIdDb:
        error = error.arg("TxID indexer");
        break;
    case Blockchain::IndexerAddressDb:
        error = error.arg("Addresses indexer");
        break;
    case Blockchain::IndexerSpentDb:
        error = error.arg("Spent-db indexer");
        break;
    }

    const bool temp = e.temporarily();
    QTimer::singleShot(0, this, [=]() {
        returnTemplatePath(socket(), temp ? "error.json" : "setup.html", error);
    });
}

void RestServiceWebRequest::threadSafeFinished()
{
    switch (answerType) {
    case TransactionDetails: {
        if (answer.size() == 0) {
            logDebug() << "Request for Transaction did not find any results";
            socket()->writeError(HttpEngine::Socket::BadRequest);
        } else {
            QJsonObject root = renderTransactionToJSon(answer.front());
            auto header = blockHeaders.find(answer.front().blockHeight);
            if (header != blockHeaders.end())
                toJson(header->second, root);
            socket()->writeJson(QJsonDocument(root), s_JsonFormat);
        }
        break;
    }
    case TransactionDetailsList: {
        QJsonArray root;
        for (const auto &tx : answer) {
            if (tx.fullTxData.size() > 0) {
                QJsonObject o = renderTransactionToJSon(tx);
                auto header = blockHeaders.find(tx.blockHeight);
                if (header != blockHeaders.end())
                    toJson(header->second, o);
                root.append(o);
            }
        }
        socket()->writeJson(QJsonDocument(root), s_JsonFormat);
        break;
    }
    case AddressDetails:
    case AddressDetailsList: {
        auto *ald = dynamic_cast<AddressListingData*>(answerData);
        assert(ald);
        QJsonArray root; // should we want a list
        for (auto &item : ald->items) {
            QJsonObject oneAddress;
            QJsonArray transactionHashes;
            qint64 balance = 0;
            qint64 received = 0;
            qint64 sent = 0;
            qint64 balanceUnconfirmed = 0;
            for (const auto &utxo : item.utxos) {
                if (utxo.blockHeight == -1) {
                    assert(utxo.unspent);
                    balanceUnconfirmed += utxo.amount;
                    continue;
                }
                if (utxo.unspent)
                    balance += utxo.amount;
                else
                    sent += utxo.amount;
                received += utxo.amount;
            }
            // a map sorts by key. Key uses blockheight
            QMap<TransactionId, Blockchain::Transaction*> sortedTx;
            for (auto i : item.transactions) {
                Blockchain::Transaction *tx = &answer[i];
                sortedTx.insert({tx->blockHeight, int(tx->offsetInBlock)}, tx);
            }

            // we want to list the most recent hits first, which are the highest blockchain ones.
            if (!sortedTx.empty()) {
                auto iter = sortedTx.end();
                do {
                    Blockchain::Transaction *tx = *(--iter);
                    transactionHashes.append(uint256ToString(tx->txid));
                } while (sortedTx.begin() != iter);
            }

            oneAddress.insert("balance", balance / 1E8);
            oneAddress.insert("balanceSat", balance);
            oneAddress.insert("totalReceived", received / 1E8);
            oneAddress.insert("totalReceivedSat", received);
            oneAddress.insert("totalSent", sent / 1E8);
            oneAddress.insert("totalSentSat", sent);
            // root.insert("txAppearances", appearences); // no clue what this means
            oneAddress.insert("transactions", transactionHashes);
            oneAddress.insert("legacyAddress", ripeToLegacyAddress(item.address.hash, item.address.type));
            oneAddress.insert("cashAddress", ripeToCashAddress(item.address.hash, item.address.type));

            oneAddress.insert("unconfirmedBalance", balanceUnconfirmed / 1E8);
            oneAddress.insert("unconfirmedBalanceSat", (double) balanceUnconfirmed);
            // "unconfirmedTxApperances":0,

            if (answerType == AddressDetails) { // just the one
                socket()->writeJson(QJsonDocument(oneAddress), s_JsonFormat);
                socket()->close();
                return;
            }
            root.append(oneAddress);
        }
        socket()->writeJson(QJsonDocument(root), s_JsonFormat);
        break;
    }
    // case AddressDetailsList: TODO
    case AddressUTXO: {
        QJsonObject root;
        QJsonArray utxos;
        auto *ald = dynamic_cast<AddressListingData*>(answerData);
        assert(ald);
        auto firstAddress = ald->cur();
        // sort from latest to oldest utxo entry
        std::sort(firstAddress->utxos.begin(), firstAddress->utxos.end(), [](const UTXOEntry& a, const UTXOEntry& b) {
            return a.blockHeight < b.blockHeight;
        });
        for (const auto &utxo : firstAddress->utxos) {
            QJsonObject o;
            o.insert("vout", utxo.outIndex);
            o.insert("satoshis", utxo.amount);
            o.insert("amount", utxo.amount / 1E8);
            o.insert("height", utxo.blockHeight);

            // TODO this is quick/and/dirty, this should be done with some lookup table
            for (const auto &tx : answer) {
                if (tx.blockHeight == utxo.blockHeight && tx.offsetInBlock == utxo.offsetInBlock) {
                    o.insert("txid", uint256ToString(tx.txid));
                    break;
                }
            }
            utxos.append(o);
        }

        root.insert("utxos", utxos);
        root.insert("legacyAddress", ripeToLegacyAddress(firstAddress->address.hash, firstAddress->address.type));
        root.insert("cashAddress", ripeToCashAddress(firstAddress->address.hash, firstAddress->address.type));
        // TODO
        //   1. make 1 tx fetch the output-scripts
        //       extract the scriptPubKey and asm from it.
        socket()->writeJson(QJsonDocument(root), s_JsonFormat);
        break;
    }
    case GetRawTransaction: {
        if (answer.size() == 0) {
            logDebug() << "Request for rawTransaction did not find any results";
            socket()->writeError(HttpEngine::Socket::BadRequest);
        } else {
            writeAsHexString(answer.front().fullTxData, socket());
        }
        break;
    }
    case GetRawTransactionVerbose: {
        if (answer.size() == 0) {
            logDebug() << "Request for rawTransactionVerbose did not find any results";
            socket()->writeError(HttpEngine::Socket::BadRequest);
        } else {
            QJsonObject root = renderTransactionToJSon(answer.front());
            auto header = blockHeaders.find(answer.front().blockHeight);
            if (header != blockHeaders.end())
                toJson(header->second, root);
            socket()->writeJson(QJsonDocument(root), s_JsonFormat);
        }
        break;
    }
    case SendRawTransaction: {
        for (size_t i = 0; i < jobs.size(); ++i) {
            // const Blockchain::Job &job = jobs.at(i);
            auto errIter = errors.find(i);
            if (errIter != errors.end()) {
                const std::string &error = errIter->second.error;
                QString qs;
                if (error == "16: missing-inputs")
                    qs = "Missing inputs";
                // other replacements go here
                if (qs.isEmpty()) {
                    qs = QString::fromStdString(error);
                    // uppercase first char
                    if (!qs.isEmpty())
                        qs[0] = qs[0].toUpper();
                }
                QJsonObject root;
                root.insert("error", qs);
                socket()->writeJson(QJsonDocument(root), s_JsonFormat);
                break;
            }
            for (const auto &tx : answer) {
                if (tx.jobId == int(i) && tx.txid.size() == 32) {
                    socket()->write("\"", 1);
                    writeAsHexStringReversed(tx.txid, socket());
                    socket()->write("\"", 1);
                    break;
                }
            }
        }
        break;
    }
    default:
        break;
    }
    socket()->close();
}

QJsonObject RestServiceWebRequest::renderTransactionToJSon(const Blockchain::Transaction &tx) const
{
    QJsonObject answer;
    if (!tx.txid.isEmpty()) {
        answer.insert("txid", uint256ToString(tx.txid));
        if (answerType == GetRawTransactionVerbose)
            answer.insert("hash", answer["txid"]);
    }
    answer.insert("size", tx.fullTxData.size());
    answer.insert("blockheight", tx.blockHeight);
    if (tx.blockHeight > 0) {
        answer.insert("firstSeenTime", QJsonValue::Null);
    }
    else {
        // for mempool transactions
        answer.insert("firstSeenTime", (double) tx.firstSeenTime);
        answer.insert("time", (double) tx.firstSeenTime);
    }
    if (tx.fees >= 0)
        answer.insert("fees", tx.fees);

    Tx::Iterator iter(tx.fullTxData);
    QJsonArray inputs, outputs;
    QJsonObject input, output;
    qint64 valueOut = 0;
    qint64 valueIn = 0;
    int inIndex = 0;
    while (iter.next() != Tx::End) {
        switch (iter.tag()) {
        case Tx::TxVersion:
            answer.insert("version", iter.intData());
            break;
        case Tx::PrevTxHash:
            input = QJsonObject();
            if (!tx.isCoinbase())
                input.insert("txid", uint256ToString(iter.byteData()));
            break;
        case Tx::PrevTxIndex:
            if (!tx.isCoinbase()) {
                input.insert("vout", iter.intData());

                // Find transaction on the other side.
                auto i = tx.txRefs.find(tx.refKeyForInput(inIndex++));
                if (i != tx.txRefs.end() && int(i->second->outputs.size()) > iter.intData()) {
                    const auto &out = i->second->outputs[iter.intData()];
                    input.insert("value", static_cast<qint64>(out.amount));
                    valueIn += out.amount;

                    QJsonArray legacyAddresses;
                    QJsonArray cashAddresses;
                    parseOutScriptAddAddresses(legacyAddresses, cashAddresses, out.outScript);
                    if (legacyAddresses.size() == 1) {
                        assert(cashAddresses.size() == 1);
                        input.insert("legacyAddress", legacyAddresses.takeAt(0));
                        input.insert("cashAddress", cashAddresses.takeAt(0));
                    }
                }
            }
            break;
        case Tx::TxInScript: {
            input.insert("n", inputs.size());
            if (tx.isCoinbase()) {
                input.insert("coinbase", "04ffff001d010b"); // WTF is that value?
            }
            else {
                QJsonObject scriptSig;
                auto bytearray = iter.byteData();
                scriptSig.insert("hex", QString::fromStdString(HexStr(bytearray.begin(), bytearray.end())));
                // TODO parse and find address
                input.insert("scriptSig", scriptSig);
            }
            break;
        }
        case Tx::Sequence:
            input.insert("sequence", static_cast<qint64>(iter.longData()));
            inputs.append(input);
            break;
        case Tx::OutputValue:
            output = QJsonObject();
            if (answerType == TransactionDetails || answerType == TransactionDetailsList)
                output.insert("value", satoshisToBCH(iter.longData()));
            else // GetRawTransactionVerbose
                output.insert("value", (double) iter.longData() / 1E8);
            output.insert("n", outputs.size());
            valueOut += iter.longData();
            break;
        case Tx::OutputScript: {
            QJsonObject outScript;
            // TODO asm
            auto bytearray = iter.byteData();
            outScript.insert("hex", QString::fromStdString(HexStr(bytearray.begin(), bytearray.end())));

            QJsonArray address1;
            QJsonArray address2;
            auto type = parseOutScriptAddAddresses(address1, address2, bytearray);

            if (answerType == TransactionDetails || answerType == TransactionDetailsList) {
                outScript.insert("addresses", address1);
                outScript.insert("cashAddrs", address2);
            } else { // GetRawTransactionVerbose
                outScript.insert("addresses", address2);
            }
            outScript.insert("type", type);
            output.insert("scriptPubKey", outScript);

            if (answerType == TransactionDetails || answerType == TransactionDetailsList) {
                /*
                 * Find the spent data
                 */
                auto i = tx.txRefs.find(tx.refKeyForOutput(outputs.size()));
                QJsonValue txid = QJsonValue::Null;
                QJsonValue index = QJsonValue::Null;
                QJsonValue height = QJsonValue::Null;
                if (i != tx.txRefs.end()) {
                    auto *spendingTx = i->second;
                    txid = uint256ToString(spendingTx->txid);
                    index = 0; // TODO, do we care which input index spent this output?
                    height = spendingTx->blockHeight;
                }
                output.insert("spentTxId", txid);
                output.insert("spentIndex", index);
                output.insert("spentHeight", height);
            }

            outputs.append(output);
            break;
        }
        case Tx::LockTime:
            answer.insert("locktime", static_cast<qint64>(iter.longData()));
            break;
        case Tx::End:
            break;
        }
    }
    answer.insert("vin", inputs);
    answer.insert("vout", outputs);
    if (answerType == TransactionDetails || answerType == TransactionDetailsList) {
        answer.insert("valueOut", (double) valueOut / 1E8);
        answer.insert("valueIn", (double) valueIn / 1E8);
        answer.insert("fees", ((double) valueIn - valueOut) / 1E8);
        // When fees is zero, we actually say its null.
        const double fees = ((double) valueIn - valueOut) / 1E8;
        if (qFuzzyIsNull(fees))
            answer.insert("fees", QJsonValue::Null);
        else
            answer.insert("fees", fees);
    }

    if (tx.isCoinbase())
        answer.insert("isCoinBase", true);
    return answer;
}
