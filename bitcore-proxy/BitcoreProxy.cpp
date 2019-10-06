/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
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
#include "BitcoreProxy.h"
#include <httpengine/socket.h>

#include <netbase.h>
#include <uint256.h>
#include <utilstrencodings.h>
#include <cashaddr.h>

#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTimer>
#include <base58.h>

#include <primitives/FastTransaction.h>
#include <primitives/pubkey.h>

namespace {

// for now assume cashaddress
static QString ripeToAddress(const std::vector<quint8> &in, CashAddress::AddressType type)
{
    return QString::fromStdString(CashAddress::encodeCashAddr("bitcoincash", { type, in })).mid(12);
}

Streaming::ConstBuffer hexStringToBuffer(const QString &hash)
{
    assert(hash.size() == 64);
    Streaming::BufferPool pool(32);
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

QString uint256ToString(const uint256 &hash)
{
    return QString::fromStdString(hash.ToString());
}

QChar hexChar(uint8_t k) {
    assert(k < 16);
    if (k < 10)
        return QChar('0' + k);
    return QChar('a' + k - 10);
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

QJsonObject toJson(const Blockchain::Transaction &tx, const QJsonObject &templateMap) {
    QJsonObject answer;
    answer.insert("coinbase", tx.offsetInBlock > 0 && tx.offsetInBlock < 90);
    if (!tx.txid.isEmpty())
        answer.insert("txid", uint256ToString(tx.txid));
    else if (templateMap.contains("txid"))
        answer.insert("txid", templateMap.value("txid"));
    answer.insert("blockHeight", tx.blockHeight);
    answer.insert("coinbase", tx.offsetInBlock > 80 && tx.offsetInBlock < 100);

    if (!tx.fullTxData.isEmpty()) {
        answer.insert("size", tx.fullTxData.size());
        Tx fullTx(tx.fullTxData);
        Tx::Iterator iter(fullTx);
        int inputCount = 0;
        int outputCount = 0;
        long value = 0;
        while (iter.next() != Tx::End) {
            if (iter.tag() == Tx::OutputValue) {
                outputCount++;
                value += iter.longData();
            }
            else  if (iter.tag() == Tx::PrevTxHash)
                inputCount++;
        }
        answer.insert("locktime", -1); // TODO not sure what this means
        answer.insert("inputCount", inputCount);
        answer.insert("outputCount", outputCount);
        // obj.insert("fee", -1); // this one is tricky...
        answer.insert("value", (qint64) value);
        if (!answer.contains("txid"))
            answer.insert("txid", QString::fromStdString(fullTx.createHash().ToString()));
    }
    return answer;
}

QJsonObject toJson(const Blockchain::BlockHeader &header, const QJsonObject &orig) {
    QJsonObject answer = orig;

    QDateTime dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(header.time), Qt::UTC);
    QString date = dt.toString(Qt::ISODateWithMs);
    answer.insert("blockTime", date);
    answer.insert("blockTimeNormalized", date);
    answer.insert("confirmations", header.confirmations);
    if (!answer.contains("blockHash"))
        answer.insert("blockHash", uint256ToString(header.hash));

    return answer;
}

void addRequestId(QJsonObject &object) {
    static QAtomicInt s_requestId = QAtomicInt(0);
    object.insert("_id", QString::number(s_requestId.fetchAndAddRelaxed(1), 16));
}

void parseScriptAndAddress(QJsonObject &object, const Streaming::ConstBuffer &script)
{
    CScript scriptPubKey(script);

    std::vector<std::vector<unsigned char> > vSolutions;
    Script::TxnOutType whichType;
    bool recognizedTx = Script::solver(scriptPubKey, whichType, vSolutions);
    if (recognizedTx && (whichType == Script::TX_PUBKEY || whichType == Script::TX_PUBKEYHASH)) {
        if (whichType == Script::TX_PUBKEYHASH) {
            Q_ASSERT(vSolutions[0].size() == 20);
            object.insert("address", ripeToAddress(vSolutions[0], CashAddress::PUBKEY_TYPE));
        } else if (whichType == Script::TX_PUBKEY) {
            CPubKey pubKey(vSolutions[0]);
            Q_ASSERT (pubKey.IsValid());
            CKeyID address = pubKey.GetID();
            std::vector<quint8> id(address.begin(), address.end());
            object.insert("address", ripeToAddress(id, CashAddress::PUBKEY_TYPE));
        }
    }
    object.insert("script", QString::fromLatin1(QByteArray(script.begin(), script.size()).toHex()));
}
}

static QJsonDocument::JsonFormat s_JsonFormat = QJsonDocument::Compact;

class UserInputException : public std::runtime_error
{
public:
    // UserInputException() = default;
    UserInputException(const char *error, const char *helpPage)
        : std::runtime_error(error),
        m_helpPage(helpPage)
    {
    }

    const char *helpPage() const {
        return m_helpPage;
    }

private:
    const char *m_helpPage = nullptr;
};


BitcoreProxy::BitcoreProxy()
{
}

void BitcoreProxy::onIncomingConnection(HttpEngine::WebRequest *request_)
{
    Q_ASSERT(request_);
    auto request = qobject_cast<BitcoreWebRequest*>(request_);
    Q_ASSERT(request);
    auto socket = request->socket();
    QObject::connect(socket, SIGNAL(disconnected()), request, SLOT(deleteLater()));

    if (socket->method() != HttpEngine::Socket::HEAD &&  socket->method() != HttpEngine::Socket::GET)
        socket->close();
    socket->setHeader("server", "Flowee");
    if (socket->path() == QLatin1String("/api/status/enabled-chains")) {
        returnEnabledChains(request);
        return;
    }
    RequestString rs(socket->path());
    if (rs.wholePath.isEmpty() || rs.request.isEmpty()) {
        returnTemplatePath(socket, "index.html");
        return;
    }
    if (rs.chain != "BCH" || rs.network != "mainnet") {
        request->socket()->writeError(404);
        return;
    }
    const QString now = QString("%1 GMT").arg(QDateTime::currentDateTimeUtc().toString("ddd, d MMM yyyy h:mm:ss"));
    socket->setHeader("last-modified", now.toLatin1()); // no cashing.
    if (socket->method() == HttpEngine::Socket::HEAD) {
        socket->writeHeaders();
        socket->close();
        return;
    }
    logInfo().nospace() << "GET\t" << socket->peerAddress().toString() << "\t" << rs.anonPath()
                        << "\t" << socket->headers().value("User-Agent").data();

    try {
        request->map().insert("network", rs.network);
        request->map().insert("chain", rs.chain);
        if (rs.request == "tx") {
            requestTransactionInfo(rs, request);
        } else if (rs.request == "address") {
            requestAddressInfo(rs, request);
        } else if (rs.request == "block") {
            requestBlockInfo(rs, request);
        } else if (rs.request == "wallet") {
            socket->writeError(501);
        } else if (rs.request == "fee") {
            returnFeeSuggestion(rs, request);
        } else if (rs.request == "stats/daily-transactions") {
            returnDailyTransactions(rs, request);
        }

        if (request->answerType == BitcoreWebRequest::Unset) {
            returnTemplatePath(socket, "index.html");
            return;
        }

        start(request);
    } catch (const UserInputException &e) {
        returnTemplatePath(socket, e.helpPage(), e.what());
    } catch (const std::exception &e) {
        logCritical() << "Failed to handle request because of" << e;
        socket->writeError(503);
    }
}

void BitcoreProxy::returnEnabledChains(HttpEngine::WebRequest *request) const
{
    // TODO make sure we detect this or configure this instead of hard coding it.
    QJsonObject chain;
    chain.insert("chain", "BCH");
    chain.insert("network", "mainnet");
    QJsonArray top;
    top.append(chain);
    request->socket()->writeJson(QJsonDocument(top), s_JsonFormat);
}

void BitcoreProxy::returnTemplatePath(HttpEngine::Socket *socket, const QString &templateName, const QString &error) const
{
    QFile helpMessage(":/" + templateName);
    if (!helpMessage.open(QIODevice::ReadOnly)) {
        logCritical() << "Missing template file" << templateName;
        socket->close();
        return;
    }
    auto data = helpMessage.readAll();
    data.replace("%ERROR%", error.toUtf8());
    socket->setHeader("Content-Length", QByteArray::number(data.size()));
    if (templateName.endsWith(".html"))
        socket->setHeader("Content-Type", "text/html");
    else
        socket->setHeader("Content-Type", "application/json");
    socket->setHeader("last-modified", "Fri, 31 May 2019 18:33:01 GMT");
    socket->writeHeaders();
    if (socket->method() != HttpEngine::Socket::HEAD)
        socket->write(data);
    socket->close();
}

void BitcoreProxy::parseConfig(const QString &confFile)
{
    QSettings conf(confFile, QSettings::IniFormat);
    conf.beginGroup("json");
    s_JsonFormat = conf.value("compact", true).toBool() ? QJsonDocument::Compact : QJsonDocument::Indented;
    conf.endGroup();
}

void BitcoreProxy::initializeHubConnection(NetworkConnection &con)
{
    con.send(Message(Api::BlockChainService, Api::BlockChain::GetBlockCount));
    con.send(Message(Api::BlockNotificationService, Api::BlockNotification::Subscribe));
}

void BitcoreProxy::requestTransactionInfo(const RequestString &rs, BitcoreWebRequest *request)
{
    if (rs.post.isEmpty()) {
        auto map = request->socket()->queryString();
        QString strBlockHeight = map.value("blockHeight");
        if (!strBlockHeight.isEmpty()) {
            request->answerType = BitcoreWebRequest::TxForHeight;
            bool ok;
            Blockchain::Job job;
            job.type = Blockchain::FetchBlockOfTx;
            job.transactionFilters = Blockchain::IncludeFullTransactionData;
            job.intData = strBlockHeight.toInt(&ok);
            if (!ok)
                throw UserInputException("blockchain not a number", "txHelp.html");
            request->jobs.push_back(job);
            request->map().insert("blockHeight", job.intData);
            job.type = Blockchain::FetchBlockHeader;
            request->jobs.push_back(job);
        }
        else {
            QString strBlockHash = map.value("blockHash");
            if (!strBlockHash.isEmpty()) {
                if (strBlockHash.length() != 64 || QByteArray::fromHex(strBlockHash.toLatin1()).length() != 32)
                    throw UserInputException("blockHash not a hash", "txHelp.html");
                request->map().insert("blockHash", strBlockHash);
                request->answerType = BitcoreWebRequest::TxForBlockHash;

                Blockchain::Job job;
                job.type = Blockchain::FetchBlockOfTx;
                job.transactionFilters = Blockchain::IncludeFullTransactionData | Blockchain::IncludeTxId;
                job.data = hexStringToBuffer(strBlockHash);
                request->jobs.push_back(job);
                job.type = Blockchain::FetchBlockHeader;
                request->jobs.push_back(job);
            }
            else
                throw UserInputException("", "txHelp.html");
        }
    } else {
        QString hashStr = rs.post.left(64);
        if (hashStr.length() < 64 || QByteArray::fromHex(hashStr.toLatin1()).length() != 32)
            throw UserInputException("No argument found", "txHelp.html");

        request->map().insert("txid", hashStr);

        Blockchain::Job job;
        job.type = Blockchain::FetchTx;
        job.data = hexStringToBuffer(hashStr);
        job.transactionFilters = Blockchain::IncludeFullTransactionData;
        if (rs.post.endsWith("authhead")) {
            request->answerType = BitcoreWebRequest::TxForTxIdAuthHead;
            // TODO Check the bugreport on what this is supposed to do. I suspect we need a new job.type
            job.transactionFilters = Blockchain::IncludeFullTransactionData; // TODO
        }
        else if (rs.post.endsWith("coins")) {
            request->answerType = BitcoreWebRequest::TxForTxIdCoins;
        }
        else {
            request->answerType = BitcoreWebRequest::TxForTxId;
        }

        job.nextJobId = 1; // that would be the 'fetchBlockHeader'
        request->jobs.push_back(job);
        job = Blockchain::Job();
        job.type = Blockchain::FetchBlockHeader;
        request->jobs.push_back(job);
    }
}

void BitcoreProxy::requestAddressInfo(const RequestString &rs, BitcoreWebRequest *request)
{
    if (rs.post.isEmpty())
        throw UserInputException("Missing address", "addressHelp.html");
    auto args = rs.post.split("/", QString::SkipEmptyParts);
    Q_ASSERT(!args.isEmpty());

    if (args.size() > 1) {
        if (args.at(1) == "txs")
            request->answerType = BitcoreWebRequest::AddressTxs;
        else if (args.at(1) == "balance")
            request->answerType = BitcoreWebRequest::AddressBalance;
    }
    else {
        if (request->socket()->queryString().contains("unspent"))
            request->answerType = BitcoreWebRequest::AddressUnspentOutputs;
    }
    if (request->answerType == BitcoreWebRequest::Unset)
        throw UserInputException("Unknown request", "addressHelp.html");

    Blockchain::Job job;
    CashAddress::Content c = CashAddress::decodeCashAddrContent(args.first().toStdString(), "bitcoincash");
    // TODO what about p2sh?
    if (c.type == CashAddress::PUBKEY_TYPE && c.hash.size() == 20) { // 20 bytes because rip160 is 20 bytes
        Streaming::BufferPool pool(20);
        memcpy(pool.begin(), c.hash.data(), 20);
        job.data = pool.commit(20);
    }
    else { // try to fall back to legacy address encoding (btc compatible)
        CBase58Data old;
        if (old.SetString(args.first().toStdString())) {
            if (old.isMainnetPkh()/* || old.isMainnetSh()*/) {
                // 20 bytes because rip160 is 20 bytes
                Streaming::BufferPool pool(20);
                memcpy(pool.begin(), old.data().data(), 20);
                job.data = pool.commit(20);
                c.hash = old.data();
            }
        }
    }

    if (job.data.isEmpty())
        throw UserInputException("Address could not be parsed", "addressHelp.html");

    job.type = Blockchain::LookupByAddress;
    request->jobs.push_back(job);
    request->map().insert("address", ripeToAddress(c.hash, CashAddress::PUBKEY_TYPE));
}

void BitcoreProxy::requestBlockInfo(const RequestString &rs, BitcoreWebRequest *request)
{
 // TODO
}

void BitcoreProxy::returnFeeSuggestion(const RequestString &rs, BitcoreWebRequest *request)
{
 // TODO
}

void BitcoreProxy::returnDailyTransactions(const RequestString &rs, BitcoreWebRequest *request)
{
 // TODO
}



// ------------------------------------------

RequestString::RequestString(const QString &path)
{
    if (path.startsWith("/api/")) {
        wholePath = path;
        int slash = path.indexOf("/", 5);
        chain = path.mid(5, slash - 5); // typically BCH
        int slash2 = path.indexOf("/", ++slash);
        if (slash2 > 0) {
            network = path.mid(slash, slash2 - slash); // typically 'mainnet'
            slash = path.indexOf("/", ++slash2);
            request = path.mid(slash2, slash - slash2);
            if (slash > 0)
                post = path.mid(slash+1);
        }
    }
}

QString RequestString::anonPath() const
{
    int i = post.indexOf('/');
    return chain + "/" + network + "/" + request + "/" + (i > 0 ? (QString("{HASH}") + post.mid(i)) : "");
}

BitcoreWebRequest::BitcoreWebRequest(qintptr socketDescriptor, std::function<void (HttpEngine::WebRequest *)> &handler)
    : HttpEngine::WebRequest(socketDescriptor, handler)
{
}

QJsonObject &BitcoreWebRequest::map()
{
    return m_map;
}

void BitcoreWebRequest::finished(int unfinishedJobs)
{
    Q_UNUSED(unfinishedJobs)
    // SearchEngine does everything in the threads that it uses for individual connections.
    // Our http engine wants to use its own thread, so lets move threads.

    QTimer::singleShot(0, this, SLOT(threadSafeFinished()));

    // TODO maybe remember unfinishedJobs being non-zero so we can deal with not found items
}

void BitcoreWebRequest::transactionAdded(const Blockchain::Transaction &transaction)
{
    if ((answerType == TxForTxIdCoins
         || answerType == AddressTxs)
            && !transaction.fullTxData.isEmpty()) {
        logDebug() << "Fetched Tx:" << transaction.blockHeight << transaction.offsetInBlock << "=>" << uint256ToString(transaction.txid);

        auto txRef = txRefs.find(std::make_pair(transaction.blockHeight, transaction.offsetInBlock));

        Tx tx(transaction.fullTxData);
        if (answerType == TxForTxIdCoins) { // insert all outputs with 'spent' placeholders into txRefs to be updated later
            assert(txRef == txRefs.end());
            int outIndex = 0;
            std::map<int, std::pair<int, int>> outputs;
            Tx::Iterator iter(tx);
            while (iter.next() != Tx::End) {
                if (iter.tag() == Tx::OutputValue)
                    outputs.insert(std::make_pair(outIndex++, std::make_pair(-2, 0)));
            }
            txRefs.insert(std::make_pair(std::make_pair(transaction.blockHeight, transaction.offsetInBlock), outputs));
        }

        int outputIndex = 0;
        Tx::Iterator iter(tx);
        while (iter.next() != Tx::End) {
            if (!transaction.isCoinbase() && iter.tag() == Tx::PrevTxHash && answerType == TxForTxIdCoins) {
                logDebug() << "Finding prev output, location of txid:" << iter.uint256Data();
                // I want to know what the block height was of this input.
                // this is needed by TxFoTxIdCoins
                Blockchain::Job job;
                job.data = iter.byteData();
                job.type = Blockchain::LookupTxById;
                logDebug() << "additionally, fetch the outputs of that TX";
                job.nextJobId = jobs.size() + 1;
                jobs.push_back(job);
                job = Blockchain::Job();
                job.type = Blockchain::FetchTx;
                job.transactionFilters = Blockchain::IncludeOutputs;
                jobs.push_back(job);
            }
            else if (iter.tag() == Tx::OutputValue) {
                // I want to know if it was spent, and if so, at what height
                // this is needed by TxFoTxIdCoins and AddressTxs
                if (answerType == TxForTxIdCoins
                        // for AddressTxs we only generate new jobs for the outputs we found in the FindAddress lookup
                        || (answerType == AddressTxs && txRef != txRefs.end()
                            && txRef->second.find(outputIndex) != txRef->second.end())) {
                    logDebug() << "   for output, lets find who spent it." << transaction.blockHeight << transaction.offsetInBlock << "outIndex:" << outputIndex;
                    Blockchain::Job job;
                    job.data = transaction.txid;
                    assert(job.data.size() == 32);
                    job.intData = outputIndex;
                    job.type = Blockchain::LookupSpentTx;
                    job.nextJobId = jobs.size() + 1;
                    jobs.push_back(job);
                    job = Blockchain::Job();
                    job.type = Blockchain::FetchTx;
                    job.transactionFilters = Blockchain::IncludeTxId;
                    jobs.push_back(job);
                }
                outputIndex++;
            }
        }
    }
}

void BitcoreWebRequest::txIdResolved(int jobId, int blockHeight, int offsetInBlock)
{
    assert(jobId >= 0);
    assert(static_cast<int>(jobs.size()) > jobId);
    assert(jobs.at(static_cast<size_t>(jobId)).data.size() == 32);
    blockHeights.insert(std::make_pair(
            uint256(jobs.at(static_cast<size_t>(jobId)).data.begin()), blockHeight));
    logDebug().nospace() << "txid resolved "
                         << uint256(jobs.at(static_cast<size_t>(jobId)).data.begin())
                         << " is tx: (" << blockHeight << ", " << offsetInBlock << ")";
}

void BitcoreWebRequest::spentOutputResolved(int jobId, int blockHeight, int offsetInBlock)
{
    if (blockHeight == -1)
        return;
    assert(jobId >= 0);
    assert(static_cast<int>(jobs.size()) > jobId);
    // Job request is data = txid. intData = outIndex
    Blockchain::Job &job = jobs.at(static_cast<size_t>(jobId));
    const int outIndex = job.intData;
    assert(job.data.size() == 32);
    auto iter = transactionMap.find(uint256(job.data.begin()));
    if (iter == transactionMap.end())
        return;

    assert(iter->second >= 0);
    assert(iter->second < answer.size());
    Blockchain::Transaction &transaction = answer.at(iter->second);

    assert(outIndex >= 0);
    auto txIter = txRefs.find(std::make_pair(transaction.blockHeight, transaction.offsetInBlock));
    assert (txIter != txRefs.end());
    auto row = txIter->second.find(outIndex);
    assert(row != txIter->second.end());
    row->second.first = blockHeight; // update placeholder inserted in addressUsedInOutput
    row->second.second = offsetInBlock;
    logDebug() << "output spent resolved" << outIndex << "->" << blockHeight << offsetInBlock;
}

void BitcoreWebRequest::addressUsedInOutput(int blockHeight, int offsetInBlock, int outIndex)
{
    logDebug().nospace() << "FindByAddress returned tx:(" << blockHeight << ", " << offsetInBlock << ") outIndex: " << outIndex;
    Q_ASSERT(blockHeight > 0);
    Q_ASSERT(offsetInBlock > 0);

    auto iter = txRefs.find(std::make_pair(blockHeight, offsetInBlock));
    if (iter != txRefs.end()) {
        // only fetch a tx once
        // but we do insert the outIndex
        iter->second.insert(std::make_pair(outIndex, std::make_pair(-2, 0)));
        return;
    }
    else { // insert outindex
        std::map<int, std::pair<int, int>> output;
        output.insert(std::make_pair(outIndex, std::make_pair(-2, 0)));
        txRefs.insert(std::make_pair(std::make_pair(blockHeight, offsetInBlock), std::move(output)));
    }

    Blockchain::Job job;
    switch (answerType) {
    case AddressTxs:
        // fetch the transaction like its a normal fetch
        job.type = Blockchain::FetchTx;
        job.intData = blockHeight;
        job.intData2 = offsetInBlock;
        job.transactionFilters = Blockchain::IncludeFullTransactionData;
        break;
    case AddressUnspentOutputs:
        job.type = Blockchain::FetchUTXOUnspent;
        job.intData = blockHeight;
        job.intData2 = offsetInBlock;
        job.intData3 = outIndex;
        break;
    case AddressBalance:
        break;
    default:
        assert(false); // nobody else should get this.
        break;
    }

    if (job.type != Blockchain::Unset) {
        // we want to fetch the highest blockHeight ones first
        auto insertBeforeIter = --jobs.end();
        while (insertBeforeIter != jobs.begin()) {
            if (insertBeforeIter->type != Blockchain::FetchTx)
                break;
            if (insertBeforeIter->intData > blockHeight)
                break;
            --insertBeforeIter;
        }
        jobs.insert(++insertBeforeIter, job);
    }
}

void BitcoreWebRequest::utxoLookup(int blockHeight, int offsetInBlock, bool unspent)
{
    if (unspent && answerType == AddressUnspentOutputs) {
        // TODO avoid requesting duplicate transactions.
        logDebug() << "UTXO finished lookup:" << blockHeight << offsetInBlock << unspent;

        // fetch unspent tx
        Blockchain::Job job;
        job.type = Blockchain::FetchTx;
        job.intData = blockHeight;
        job.intData2 = offsetInBlock;
        job.transactionFilters = Blockchain::IncludeFullTransactionData;
        jobs.push_back(job);
    }
}

void BitcoreWebRequest::addDefaults(QJsonObject &node)
{
    node.insert("network", m_map.value("network"));
    node.insert("chain", m_map.value("chain"));
    addRequestId(node);
}

void BitcoreWebRequest::threadSafeFinished()
{
    switch (answerType) {
    case TxForTxId: {
        QJsonObject root;
        if (answer.size() == 1) {
            root = toJson(answer.front(), m_map);

            auto header = blockHeaders.find(answer.front().blockHeight);
            if (header != blockHeaders.end())
                root = toJson(header->second, root);
        }
        addDefaults(root);
        socket()->writeJson(QJsonDocument(root), s_JsonFormat);
        break;
    }
    case TxForHeight:
    case TxForBlockHash: {
        QJsonArray root;
        for (auto tx : answer) {
            QJsonObject o = toJson(tx, m_map);

            auto header = blockHeaders.find(tx.blockHeight);
            if (header != blockHeaders.end())
                o = toJson(header->second, o);
            addDefaults(o);
            root.append(o);
        }
        socket()->writeJson(QJsonDocument(root), s_JsonFormat);
        break;
    }
    case TxForTxIdCoins: {
        QJsonObject root;
        if (!answer.front().fullTxData.isEmpty()) {
            const Blockchain::Transaction &transaction = answer.front();
            Tx tx(transaction.fullTxData);
            uint256 hash = tx.createHash();
            QString myHash = uint256ToString(hash);
            Tx::Iterator iter(tx);
            assert(txRefs.size() == 1);
            auto txRef = txRefs.begin();
            QJsonArray inputs, outputs;
            QJsonObject cur;
            uint256 prevTx;
            int outIndex = 0;
            while (iter.next() != Tx::End) {
                if (!transaction.isCoinbase()) {
                    if (iter.tag() == Tx::PrevTxHash) {
                        cur = QJsonObject();
                        prevTx = iter.uint256Data();
                        cur.insert("coinbase", transaction.isCoinbase());
                        cur.insert("spentTxid", myHash);
                        cur.insert("mintTxid", uint256ToString(prevTx));
                        cur.insert("spentHeight", transaction.blockHeight);
                        cur.insert("confirmations", -1);  // copied from an actual bitcore server, looks wrong though!
                        auto bhIter = blockHeights.find(prevTx);
                        if (bhIter != blockHeights.end())
                            cur.insert("mintHeight", bhIter->second);
                    }
                    else if (iter.tag() == Tx::PrevTxIndex) {
                        cur.insert("mintIndex", iter.intData());
                        // Find the previous output, we should have fetched it.
                        for (const Blockchain::Transaction &t : answer) {
                            if (t.txid.size() == 32 && memcmp(t.txid.begin(), prevTx.begin(), 32) == 0) {
                                // we fetched this tx with outputs only, lets dig out what we need.
                                if (t.outputs.size() > iter.intData()) {
                                    auto output = t.outputs.at(iter.intData());
                                    cur.insert("value", (qint64) output.amount);
                                    parseScriptAndAddress(cur, output.outScript);
                                }
                                break;
                            }
                        }
                    } else if (iter.tag() == Tx::TxInScript) {
                        addDefaults(cur);
                        inputs.append(cur);
                    }
                }
                if (iter.tag() == Tx::OutputValue) {
                    cur = QJsonObject();
                    cur.insert("coinbase", transaction.isCoinbase());
                    cur.insert("confirmations", -1);  // copied from an actual bitcore server, looks wrong though!
                    cur.insert("value", static_cast<qint64>(iter.longData()));
                    cur.insert("mintHeight", transaction.blockHeight);
                    cur.insert("mintIndex", outputs.size());
                    cur.insert("mintTxid", myHash);

                    auto out = txRef->second.find(outIndex++);
                    assert(out != txRef->second.end());
                    cur.insert("spentHeight", out->second.first);
                    cur.insert("spentTxid", QString());
                    for (const Blockchain::Transaction &t : answer) {
                        if (t.blockHeight == out->second.first && t.offsetInBlock == out->second.second) {
                            cur.insert("spentTxid", uint256ToString(t.txid));
                            break;
                        }
                    }
                } else if (iter.tag() == Tx::OutputScript) {
                    parseScriptAndAddress(cur, iter.byteData());
                    addDefaults(cur);
                    outputs.append(cur);
                }
            }

            root.insert("inputs", inputs);
            root.insert("outputs", outputs);
        }
        socket()->writeJson(QJsonDocument(root), s_JsonFormat);

        break;
    }
    case AddressUnspentOutputs:
    case AddressTxs: {
        QJsonArray root;
        QString script;
        for (auto tx : answer) {
            assert(tx.jobId >= 0);
            Blockchain::Job &job = jobs[tx.jobId];
            auto refs = txRefs.find(std::make_pair(tx.blockHeight, tx.offsetInBlock));
            if (refs == txRefs.end()) // not one of main transactions
                continue;

            const QString txid = uint256ToString(tx.txid);
            Tx fullTx(tx.fullTxData);
            for (auto out : refs->second) {
                // about 'out': outputIndex is 'first' and who spent it is 'second' (a pair)
                QJsonObject o;
                o.insert("coinbase", tx.offsetInBlock > 0 && tx.offsetInBlock < 90);
                o.insert("mintHeight", tx.blockHeight);
                o.insert("address", map().value("address"));
                o.insert("mintTxid", txid);
                o.insert("mintIndex", out.first);
                o.insert("confirmations", -1); // not sure why this is -1 in Bitcore.

                auto outData = fullTx.output(out.first);
                o.insert("value", static_cast<qint64>(outData.outputValue));
                if (script.isEmpty())  // stays the same for this entire call
                    script = QString::fromLatin1(QByteArray(outData.outputScript.begin(), outData.outputScript.size()).toHex());
                o.insert("script", script);

                o.insert("spentHeight", out.second.first);
                o.insert("spentTxid", QString());
                for (const Blockchain::Transaction &t : answer) {
                    if (t.blockHeight == out.second.first && t.offsetInBlock == out.second.second) {
                        o.insert("spentTxid", uint256ToString(t.txid));
                        break;
                    }
                }

                addDefaults(o);
                root.append(o);
            }
        }
        socket()->writeJson(QJsonDocument(root), s_JsonFormat);
        break;
    }
    default:
        // TODO
        break;
    }
    socket()->close();
}
