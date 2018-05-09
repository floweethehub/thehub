/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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
#include "Importer.h"

#include <primitives/FastTransaction.h>
#include <primitives/FastBlock.h>
#include <Logger.h>
#include <BlocksDB.h>

#include <QDir>
#include <QCoreApplication>
#include <chain.h>
#include <chainparamsbase.h>
#include <QDebug>
#include <QTime>
#include <QFuture>
#include <Application.h>
#include <QtConcurrent/QtConcurrent>

namespace {
quint64 longFromHash(const uint256 &sha) {
    const quint64 *answer = reinterpret_cast<const quint64*>(sha.begin());
    return answer[0] >> 1;
}

struct Input {
    uint256 txid;
    int index;
};

std::list<Input> findInputs(Tx::Iterator &iter) {
    std::list<Input> inputs;

    Input curInput;
    auto content = iter.next();
    while (content != Tx::End) {
        if (content == Tx::PrevTxHash) { // only read inputs for non-coinbase txs
            if (iter.byteData().size() != 32)
                throw std::runtime_error("Failed to understand PrevTxHash");
            curInput.txid = iter.uint256Data();
            content = iter.next(Tx::PrevTxIndex);
            if (content != Tx::PrevTxIndex)
                throw std::runtime_error("Failed to find PrevTxIndex");
            curInput.index = iter.intData();
            inputs.push_back(curInput);
        }
        else if (content == Tx::OutputValue) {
            break;
        }
        content = iter.next();
    }
    return inputs;
}

struct MapArg {
    Importer *o;
    const CBlockIndex *index;
    Tx tx;
    bool cb;
    qint64 oib;
};

Importer::ProcessTxResult mapFunction(const MapArg &t)
{
    try {
        return t.o->processTx(t.index, t.tx, t.cb, t.oib, Importer::ReturnInserts);
    } catch (const std::exception &e) {
        logFatal() << e;
        throw;
    }
}

void reduceFunction(Importer::ProcessTxResult &result, const Importer::ProcessTxResult &intermediate)
{
    result += intermediate;
}
}


Importer::Importer(QObject *parent)
    : QObject(parent),
      m_utxo(Application::instance()->ioService(), GetDataDir() / "unspent")
{
}

void Importer::start()
{
    logInfo() << "Init";
    try {
        SelectBaseParams(CBaseChainParams::MAIN);
        Blocks::DB::createInstance(1000, false);
        logInfo() << "Reading blocksDB";
        Blocks::DB::instance()->CacheAllBlockInfos();
        logInfo() << "Finding blocks..." << "starting with" << m_utxo.blockheight();
        QTime time;
        time.start();
        const auto &chain = Blocks::DB::instance()->headerChain();
        CBlockIndex *index = chain.Genesis();
        if (index == nullptr) {
            logCritical() << "No blocks in DB. Not even genesis block!";
            QCoreApplication::exit(2);
            return;
        }

        if (m_utxo.blockheight() > 0)
            index = chain[m_utxo.blockheight()];

        int nextStop = 50000;
        int lastHeight = -1;
        while(true) {
            index = chain.Next(index); // we skip genesis, its not part of the utxo
            if (! index)
                break;
            lastHeight = index->nHeight;
            try {
                parseBlock(index, Blocks::DB::instance()->loadBlock(index->GetBlockPos()));
            } catch (const std::exception &e) {
                logFatal() << "Parse block failed with:" << e<< "Block:" << index->nHeight
                           <<  Blocks::DB::instance()->loadBlock(index->GetBlockPos()).createHash();
                throw;
            }

            if (m_txCount.load() > nextStop) {
                nextStop = m_txCount.load() + 750000;
                auto elapsed = time.elapsed();
                logCritical().nospace() << "Finished blocks 0..." << index->nHeight << ", tx count: " << m_txCount.load();
                logCritical() << "  parseBlocks" << m_parse.load() << "ms\t" << (m_parse.load() * 100 / elapsed) << '%';
                logCritical() << "       select" << m_selects.load() << "ms\t" << (m_selects.load() * 100 / elapsed) << '%';
                logCritical() << "       delete" << m_deletes.load() << "ms\t" << (m_deletes.load() * 100 / elapsed) << '%';
                logCritical() << "       insert" << m_inserts.load() << "ms\t" << (m_inserts.load() * 100 / elapsed) << '%';
                logCritical() << "        flush" << m_flush.load() << "ms\t" << (m_flush.load() * 100 / elapsed) << '%';
                logCritical() << "    filter-tx" << m_filterTx.load() << "ms\t" << (m_filterTx.load() * 100 / elapsed) << '%';
                logCritical() << "   Wall-clock" << elapsed << "ms";
            }
            if (index->nHeight >= 500000) // thats all for now
                break;
        }
        logCritical() << "Finished with block at height:" << lastHeight;
    } catch (const std::exception &e) {
        logFatal() << e;
        QCoreApplication::exit(1);
        return;
    }
    QCoreApplication::exit(0);
}

void Importer::parseBlock(const CBlockIndex *index, FastBlock block)
{
    if (index->nHeight % 1000 == 0)
        logInfo() << "Parsing block" << index->nHeight << block.createHash() << "tx-count" << m_txCount.load();

    block.findTransactions();
    const auto transactions = block.transactions();

    QSet<int> ordered;
    QTime time;
    time.start();

    if (transactions.size() > 1) {
        /* Filter the transactions.
        * Transactions by consensus are sequential, tx 2 can't spend a UTXO that is created in tx 3.
        *
        * This means that our ordering is Ok, we just want to be able to extract all the transactions
        * that spend transactions NOT created in this block, which we can then process in parallel.
        * Notice that the fact that the transactions are sorted now is awesome as that makes the process
        * much much faster.
        *
        * Additionally we check for double-spends. No 2 transactions are allowed to spend the same UTXO inside this block.
        */
        typedef boost::unordered_map<uint256, int, Blocks::BlockHashShortener> TXMap;
        TXMap txMap;

        typedef boost::unordered_map<uint256, std::vector<bool>, Blocks::BlockHashShortener> MiniUTXO;
        MiniUTXO miniUTXO;

        bool first = true;
        int txNum = 1;
        for (auto tx : transactions) {
            if (first) { // skip coinbase
                first = false;
                continue;
            }
            uint256 hash = tx.createHash();
            bool ocd = false; // if true, tx requires order.

            auto i = Tx::Iterator(tx);
            auto inputs = findInputs(i);
            for (auto input : inputs) {
                auto ti = txMap.find(input.txid);
                if (ti != txMap.end()) {
                    ocd = true;
                    /*
                     * ok, so we spend a tx also in this block.
                     * to make sure we don't hit a double-spend here I have to actually check the outputs of the prev-tx.
                     *
                     * at this time this isn't unit tested, as such you should assume it is broken.
                     * the point of this code is to make clear how we can avoid processing our transactions serially
                     * and we can avoid the need for 'rollback()' (when a block fails half-way through) because we
                     * detect in-block double-spends without touching the DB.
                     *
                     * so I can do all sql deletes in one go when the block is done validating.
                     */
                    auto prevIter = miniUTXO.find(input.txid);
                    if (prevIter == miniUTXO.end()) {
                        /*
                         *  insert into the miniUTXO the prevtx outputs.
                         * we **could** have done this at the more logical code-place for all transactions,
                         * but since we expect less than 1% of the transactions to spend inside of the same block,
                         * that would waste resources.
                         */
                        auto iter = Tx::Iterator(transactions.at(ti->second));
                        Tx::Component component;
                        std::vector<bool> outputs;
                        while (true) {
                            component = iter.next(Tx::OutputValue);
                            if (component == Tx::End)
                                break;
                            outputs.push_back(true);
                        }

                        prevIter = miniUTXO.insert(std::make_pair(input.txid, outputs)).first;

                        ordered.insert(ti->second);
                    }
                    if (prevIter->second.size() <= input.index)
                        throw std::runtime_error("spending utxo output out of range");
                    if (prevIter->second[input.index] == false)
                        throw std::runtime_error("spending utxo in-block double-spend");
                    prevIter->second[input.index] = false;
                }
            }

            if (ocd)
                ordered.insert(txNum);
            txMap.insert(std::make_pair(hash, txNum++));
        }
    }
    m_filterTx.fetchAndAddRelaxed(time.elapsed());

    ProcessTxResult commandsForallTransactions;

    QList<MapArg> unorderedTx;
    for (size_t i = 0; i < transactions.size(); ++i) {
        if (!ordered.contains(i)) {
            Tx tx = transactions.at(i);
            unorderedTx.append(MapArg {this, index, tx, i == 0, tx.offsetInBlock(block) });
       }
    }
    QFuture<ProcessTxResult> processedTx = QtConcurrent::mappedReduced(unorderedTx, mapFunction, reduceFunction);

    QList<int> sorted(ordered.toList());
    std::sort(sorted.begin(), sorted.end());
    for (int i : sorted) {
        Tx tx = transactions.at(i);
        commandsForallTransactions.leafsToDelete += processTx(index, tx, i == 0, tx.offsetInBlock(block), InsertDirect).leafsToDelete;
    }
    processedTx.waitForFinished();
    commandsForallTransactions  += processedTx;

    Q_ASSERT(commandsForallTransactions.outputs.size() > 0); // at minimum the coinbase has an output.

    time.start();
    for (auto uo: commandsForallTransactions.outputs) {
        // logDebug() << "inserting" << uo.prevTXID << uo.outIndex << uo.blockHeight;
        m_utxo.insert(uo.prevTx, uo.outIndex, uo.offsetInBlock, uo.blockHeight);
    }
    m_inserts.fetchAndAddRelaxed(time.elapsed());

    time.start();

    for (auto uo: commandsForallTransactions.leafsToDelete) {
        m_utxo.remove(uo.prevTx, uo.outIndex);
    }

    m_deletes.fetchAndAddRelaxed(time.elapsed());
    time.start();

    m_utxo.blockFinished(index->nHeight, block.createHash());
    m_flush.fetchAndAddRelaxed(time.elapsed());

    m_txCount.fetchAndAddRelaxed(block.transactions().size());
}

Importer::ProcessTxResult Importer::processTx(const CBlockIndex *index, Tx tx, bool isCoinbase, int offsetInBlock, Direct direct)
{
    uint256 txHash = tx.createHash();
    // logDebug() << "tx" << txHash << "@" << index->nHeight << (isCoinbase ? "coinbase" : "tx:") << offsetInBlock << direct;
    std::list<Input> inputs;
    int outputCount = 0;
    ProcessTxResult result;
    QTime time;
    time.start();
    {
        auto iter = Tx::Iterator(tx);
        if (!isCoinbase)
            inputs = findInputs(iter);
        auto content = iter.tag();
        while (content != Tx::End) {
            if (content == Tx::OutputValue) {
                if (iter.longData() == 0)
                    logDebug() << "Output with zero value";
                Output output;
                output.prevTx = txHash;
                output.blockHeight = index->nHeight;
                output.offsetInBlock = (int) offsetInBlock;
                output.outIndex = outputCount;
                result.outputs.append(output);
                outputCount++;
            }
            content = iter.next();
        }
    }
    m_parse.fetchAndAddRelaxed(time.elapsed());
    if (!inputs.empty()) {
        QTime selectTime;
        selectTime.start();
        for (auto input : inputs) {
            try {
                auto leaf = m_utxo.find(input.txid, input.index);
                if (leaf.blockHeight() == 0) {
                    logFatal() << "block" << index->nHeight << "tx" << txHash << "tries to find input" << input.txid << input.index;
                    logInfo() << "    " << QString::number(longFromHash(input.txid), 16).toStdString();
                    throw std::runtime_error("UTXO not found");
                }

                result.leafsToDelete.append(leaf);
            } catch(const std::exception &e) {
                logFatal() << e << input.txid << input.index;
                throw;
            }
        }
        m_selects.fetchAndAddRelaxed(selectTime.elapsed());
    }

    if (direct == InsertDirect) {
        time.start();
        // other transactions in this block require these to be in the DB
        // TODO well, that means I need some sort of way to undo these when the block fails.
        for (auto uo : result.outputs) {
            // logDebug() << "inserting direct" << uo.prevTXID << uo.outIndex << uo.blockHeight;
            m_utxo.insert(uo.prevTx, uo.outIndex, uo.offsetInBlock, uo.blockHeight);
        }
        result.outputs.clear();
        m_inserts.fetchAndAddRelaxed(time.elapsed());
    }

    return result;
}

Importer::ProcessTxResult &Importer::ProcessTxResult::operator+=(const Importer::ProcessTxResult &other)
{
    outputs += other.outputs;
    leafsToDelete += other.leafsToDelete;
    return *this;
}

Importer::Output::Output(UnspentOutput &other)
{
    prevTx = other.prevTxId();
    outIndex = other.outIndex();
    offsetInBlock = other.offsetInBlock();
    blockHeight = other.blockHeight();
}
