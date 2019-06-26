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
#include "AddressIndexer.h"
#include "Indexer.h"
#include <uint256.h>
#include <APIProtocol.h>
#include <streaming/MessageParser.h>
#include <Message.h>

#include <QTime>
#include <qsettings.h>
#include <qsqlerror.h>
#include <qtimer.h>
#include <qvariant.h>
#include <qcoreapplication.h>

namespace {

static QString createIndexString("create index %1_index on %1 (address_row)");

QString valueFromSettings(const QSettings &settings, const QString &key) {
    QVariant x = settings.value(QString("addressdb/") + key);
    if (x.isNull())
        x = settings.value(key);
    return x.toString();
}

QString addressTable(int index) {
    return QString("AddressUsage%1").arg(index, 2, 10, QChar('_'));
}

}

AddressIndexer::AddressIndexer(const boost::filesystem::path &basedir, Indexer *datasource)
    : m_addresses(basedir),
      m_basedir(QString::fromStdWString(basedir.wstring())),
      m_dataSource(datasource),
      m_flushRequesed(0)
{
}

void AddressIndexer::loadSetting(const QSettings &settings)
{
    auto db = valueFromSettings(settings, "db_driver");
    m_insertDb = QSqlDatabase::addDatabase(db, "insertConnection");
    if (!m_insertDb.isValid()) {
        if (QSqlDatabase::drivers().contains(db)) {
            logFatal().nospace() << "Failed to open a databse (" << db << "), missing libs?";
        } else {
            logFatal() << "The configured database is not known. Please select from this list:";
            logFatal() << QSqlDatabase::drivers().toStdList();
        }
        logCritical() << "Error reported:" << m_insertDb.lastError().text();
        throw std::runtime_error("Failed to read database");
    }

    m_selectDb = QSqlDatabase::addDatabase(db, "selectConnection");
    if (db == "QPSQL") {
        m_insertDb.setDatabaseName(valueFromSettings(settings, "db_database"));
        m_insertDb.setUserName(valueFromSettings(settings, "db_username"));
        m_insertDb.setPassword(valueFromSettings(settings, "db_password"));
        m_insertDb.setHostName(valueFromSettings(settings, "db_hostname"));
        m_selectDb.setDatabaseName(valueFromSettings(settings, "db_database"));
        m_selectDb.setUserName(valueFromSettings(settings, "db_username"));
        m_selectDb.setPassword(valueFromSettings(settings, "db_password"));
        m_selectDb.setHostName(valueFromSettings(settings, "db_hostname"));
    } else if (db == "QSQLITE") {
        m_insertDb.setDatabaseName(m_basedir + "/addresses.db");
        m_selectDb.setDatabaseName(m_basedir + "/addresses.db");
    }

    if (m_insertDb.isValid() && m_insertDb.open() && m_selectDb.open()) {
        createTables();
    } else {
        logFatal() << "Failed opening the database-connection" << m_insertDb.lastError().text();
        throw std::runtime_error("Failed to open database connection");
    }
}

int AddressIndexer::blockheight()
{
    if (m_height == -1) {
        QSqlQuery query(m_selectDb);
        query.prepare("select blockHeight from LastKnownState");
        if (!query.exec()) {
            logFatal() << "Failed to select" << query.lastError().text();
            QCoreApplication::exit(1);
        }
        query.next();
        m_height = query.value(0).toInt();
    }
    return m_height;
}

void AddressIndexer::blockFinished(int blockheight, const uint256 &)
{
    Q_ASSERT(blockheight > m_height);
    m_height = blockheight;
    if (++m_uncommittedCount > 150000)
        commitAllData();
}

void AddressIndexer::insert(const Streaming::ConstBuffer &addressId, int outputIndex, int blockHeight, int offsetInBlock)
{
    // an address is a hash160
    Q_ASSERT(addressId.size() == 20);
    Q_ASSERT(QThread::currentThread() == this);

    const uint160 *address = reinterpret_cast<const uint160*>(addressId.begin());
    auto result = m_addresses.lookup(*address);
    if (result.db == -1)
        result = m_addresses.append(*address);
    assert(result.db >= 0);
    assert(result.row >= 0);

    if (result.db >= m_uncommittedData.size())
        m_uncommittedData.resize(result.db + 1);

    std::deque<Entry> &data = m_uncommittedData[result.db];
    assert(outputIndex < 0x7FFF);
    data.push_back({(short) outputIndex, blockHeight, result.row, offsetInBlock});
    ++m_uncommittedCount;
}

std::vector<AddressIndexer::TxData> AddressIndexer::find(const uint160 &address) const
{
    std::vector<TxData> answer;
    auto result = m_addresses.lookup(address);
    if (result.db == -1)
        return answer;

    QSqlQuery query(m_selectDb);
    const QString select = QString("select offset_in_block, block_height, out_index "
                                  "FROM AddressUsage%1 "
                                  "WHERE address_row=:row").arg(result.db, 2, 10, QChar('_'));
    query.prepare(select);
    query.bindValue(":row", result.row);
    if (!query.exec()) {
        logFatal() << "Failed to select" << query.lastError().text();
        logDebug() << "Failed with" << select;
        QCoreApplication::exit(1);
    }
    const int size = query.size();
    if (size > 0)
        answer.reserve(size);
    while (query.next()) {
        TxData txData;
        txData.offsetInBlock = query.value(0).toInt();
        txData.blockHeight = query.value(1).toInt();
        txData.outputIndex = (short) query.value(2).toInt();
        answer.push_back(txData);
    }
    return answer;
}

void AddressIndexer::createTables()
{
    /* Tables
     * AddressUsage_N
     *  address_row     INTEGER   (the row that the hashStorage provided us with)
     *  block_height    INTEGER    \
     *  offset_in_block INTEGER    /-- together give the transaction
     *  out_index       INTEGER
     *
     * LastKnownState
     *  blockHeight    INTEGER
     */

    QSqlQuery query(m_insertDb);
    bool doInsert = false;
    if (!query.exec("select count(*) from LastKnownState")) {
        QString q("create table LastKnownState ("
                  "blockHeight INTEGER)");
        if (!query.exec(q)) {
            logFatal() << "Failed to create table" << query.lastError().text();
            throw std::runtime_error("Failed to create table");
        }
        doInsert = true;
    }
    if (doInsert || query.next() && query.value(0).toInt() < 1) {
        if (!query.exec("insert into LastKnownState values (0)")) {
            logFatal() << "Failed to insert row" << query.lastError().text();
            throw std::runtime_error("Failed to insert row");
        }
    }

    for (int db = 0;; ++db) {
        if (!query.exec(QString("select count(*) from ") + addressTable(db))) // found the last DB
            break;

        // Make sure there is an index on tables we have not finished inserting into yet.
        if (db > 0) {
            QString tableName = addressTable(db -1);
            if (query.exec(createIndexString.arg(tableName)))
                logCritical() << "Created index on SQL table" << tableName;
        }
    }
}

void AddressIndexer::flush()
{
    m_flushRequesed = 1;
}

void AddressIndexer::run()
{
    assert(m_dataSource);
    while (!isInterruptionRequested()) {
        Message message = m_dataSource->nextBlock(blockheight() + 1, 2000);

        if (m_flushRequesed.load() == 1) {
            commitAllData();
            m_flushRequesed = 0;
        }
        if (message.body().size() == 0) // typically true if the flush was requested
            continue;

        int txOffsetInBlock = 0;
        int outputIndex = -1;
        uint256 blockId;
        int blockHeight = -1;

        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::BlockHeight) {
                blockHeight = parser.intData();
                Q_ASSERT(blockHeight == m_height + 1);
            } else if (parser.tag() == Api::BlockChain::BlockHash) {
                blockId = parser.uint256Data();
            } else if (parser.tag() == Api::BlockChain::Separator) {
                txOffsetInBlock = 0;
                outputIndex = -1;
            } else if (parser.tag() == Api::BlockChain::Tx_OffsetInBlock) {
                txOffsetInBlock = parser.intData();
            } else if (parser.tag() == Api::BlockChain::Tx_Out_Index) {
                outputIndex = parser.intData();
            } else if (parser.tag() == Api::BlockChain::Tx_Out_Address) {
                assert(parser.dataLength() == 20);
                assert(outputIndex >= 0);
                assert(blockHeight > 0);
                assert(txOffsetInBlock > 0);
                insert(parser.bytesDataBuffer(), outputIndex, blockHeight, txOffsetInBlock);
            }
        }
        assert(blockHeight > 0);
        assert(!blockId.IsNull());
        blockFinished(blockHeight, blockId);
    }
}

void AddressIndexer::commitAllData()
{
    QTime time;
    time.start();
    int rowsInserted = 0;
    logInfo() << "AddressDB sending data to SQL DB";
    // create tables outside of transaction
    for (size_t db = 0; db < m_uncommittedData.size(); ++db) {
        const std::deque<Entry> &list = m_uncommittedData.at(db);
        if (!list.empty()) {
            const QString table = addressTable(db);
            QSqlQuery query(m_insertDb);
            if (!query.exec(QString("select count(*) from ") + table)) {
                static QString q("create table %1 ("
                          "address_row INTEGER, "
                          "block_height INTEGER, offset_in_block INTEGER, out_index INTEGER)");
                if (!query.exec(q.arg(table))) {
                    logFatal() << "Failed to create table" << query.lastError().text();
                    QCoreApplication::exit(1);
                }

                // when creating a new table, set the index on the previous table.
                if (db > 0) {
                    if (!query.exec(createIndexString.arg(addressTable(db - 1)))) {
                        logFatal() << "Failed to create index" << query.lastError().text();
                        QCoreApplication::exit(1);
                    }
                }
            }
        }
    }

    m_insertDb.transaction();
    for (size_t db = 0; db < m_uncommittedData.size(); ++db) {
        const std::deque<Entry> &list = m_uncommittedData.at(db);
        if (!list.empty()) {
            const QString table = addressTable(db);
            QSqlQuery query(m_insertDb);
            query.prepare("insert into " + table + " values (?, ?, ?, ?)");
            logDebug() << "bulk insert of" << list.size() << "rows into" << table;
            QVariantList row;
            row.reserve(list.size());
            QVariantList height;
            height.reserve(list.size());
            QVariantList offsetInBlock;
            offsetInBlock.reserve(list.size());
            QVariantList outIndex;
            outIndex.reserve(list.size());
            for (auto entry : list) {
                row.append(entry.row);
                height.append(entry.height);
                offsetInBlock.append(entry.offsetInBlock);
                outIndex.append(entry.outIndex);
            }
            query.addBindValue(row);
            query.addBindValue(height);
            query.addBindValue(offsetInBlock);
            query.addBindValue(outIndex);
            if (!query.execBatch()) {
                logFatal() << "Failed to insert into" << table << "reason:" << query.lastError().text();
                QCoreApplication::exit(1);
            }
            rowsInserted += list.size();
        }
    }
    m_uncommittedData.clear();
    QSqlQuery query(m_insertDb);
    query.prepare("update LastKnownState set blockHeight=:bh");
    query.bindValue(":bh", m_height);
    if (!query.exec()) {
        logFatal() << "Failed to update blockheight" << query.lastError().text();
        logDebug() << " q" << query.lastQuery();
    }

    m_insertDb.commit();
    logInfo().nospace() << "AddressDB: SQL-DB took " << time.elapsed() << "ms to insert " << rowsInserted << " rows";
}
