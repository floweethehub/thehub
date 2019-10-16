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

class TableSpecification
{
public:
    inline virtual ~TableSpecification() {}

    virtual bool queryTableExists(QSqlQuery &query, const QString &tableName) const {
        return query.exec("select count(*) from " + tableName);
    }

    // we only create one index type, so this API is assuming a lot.
    virtual bool createIndexIfNotExists(QSqlQuery &query, const QString &tableName) const {
        QString createIndexString("create index %1_index on %1 (address_row)");
        return query.exec(createIndexString.arg(tableName));
    }
};

class PostgresTables : public TableSpecification
{
public:
    bool queryTableExists(QSqlQuery &query, const QString &tableName) const override {
        bool ok = query.exec("select exists (select 1 from pg_tables where tablename='" + tableName.toLower()
                   + "' and schemaname='public')");
        if (!ok)
            return false;
        query.next();
        return query.value(0).toInt() == 1;
    }

    bool createIndexIfNotExists(QSqlQuery &query, const QString &tableName) const override {
        QString createIndexString("CREATE INDEX IF NOT EXISTS %1_index ON %1 (address_row)");
        return query.exec(createIndexString.arg(tableName.toLower()));
    }
};

AddressIndexer::AddressIndexer(const boost::filesystem::path &basedir, Indexer *datasource)
    : m_addresses(basedir),
      m_basedir(QString::fromStdWString(basedir.wstring())),
      m_dataSource(datasource),
      m_flushRequested(0)
{
}

AddressIndexer::~AddressIndexer()
{
    delete m_spec;
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
    delete m_spec;
    m_spec = nullptr;

    m_selectDb = QSqlDatabase::addDatabase(db, "selectConnection");
    logInfo().nospace() << "AddressIndexer database(" << db << ") "
                        << valueFromSettings(settings, "db_username")
                        << "@" << valueFromSettings(settings, "db_hostname")
                        << " DB: " << valueFromSettings(settings, "db_database");
    if (db == "QPSQL") {
        m_insertDb.setDatabaseName(valueFromSettings(settings, "db_database"));
        m_insertDb.setUserName(valueFromSettings(settings, "db_username"));
        m_insertDb.setPassword(valueFromSettings(settings, "db_password"));
        m_insertDb.setHostName(valueFromSettings(settings, "db_hostname"));
        m_selectDb.setDatabaseName(valueFromSettings(settings, "db_database"));
        m_selectDb.setUserName(valueFromSettings(settings, "db_username"));
        m_selectDb.setPassword(valueFromSettings(settings, "db_password"));
        m_selectDb.setHostName(valueFromSettings(settings, "db_hostname"));
        m_spec = new PostgresTables();
    } else if (db == "QSQLITE") {
        m_insertDb.setDatabaseName(m_basedir + "/addresses.db");
        m_selectDb.setDatabaseName(m_basedir + "/addresses.db");
    }

    if (m_selectDb.isValid() && m_insertDb.open() && m_selectDb.open()) {
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
        if (!query.exec("select blockheight from LastKnownState")) {
            logFatal() << "Failed to select" << query.lastError().text();
            QCoreApplication::exit(1);
        }
        query.next();
        m_height = query.value(0).toInt();
        m_topOfChain = m_spec->queryTableExists(query, "IBD") ? InInitialSync : InitialSyncFinished;
    }
    return m_height;
}

void AddressIndexer::blockFinished(int blockheight, const uint256 &)
{
    Q_ASSERT(blockheight > m_height);
    m_height = blockheight;
    if (++m_uncommittedCount > 150000) {
        commitAllData();
        m_uncommittedCount = 0;
    }
}

void AddressIndexer::insert(const Streaming::ConstBuffer &outScriptHashed, int outputIndex, int blockHeight, int offsetInBlock)
{
    Q_ASSERT(outScriptHashed.size() == 32); // a sha256
    Q_ASSERT(QThread::currentThread() == this);

    const uint256 *address = reinterpret_cast<const uint256*>(outScriptHashed.begin());
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

void AddressIndexer::reachedTopOfChain()
{
    Q_ASSERT(m_height != -1); // make sure blockHeight was called before this one
    m_topOfChain.testAndSetAcquire(InInitialSync, FlushRequested);
    m_flushRequested = 1;
}

std::vector<AddressIndexer::TxData> AddressIndexer::find(const uint256 &address) const
{
    std::vector<TxData> answer;
    auto result = m_addresses.lookup(address);
    if (result.db == -1)
        return answer;

    QSqlQuery query(m_selectDb);
    const QString select = QString("select DISTINCT offset_in_block, block_height, out_index "
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
        txData.outputIndex = static_cast<short>(query.value(2).toInt());
        answer.push_back(txData);
    }
    return answer;
}

void AddressIndexer::createTables()
{
    if (m_spec == nullptr)
        m_spec = new TableSpecification(); // generic one.

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
    if (!m_spec->queryTableExists(query, "LastKnownState")) {
        logInfo() << "Creating tables...";
        if (!query.exec("create table LastKnownState (blockheight INTEGER)")) {
            logFatal() << "Failed to create table" << query.lastError().text();
            throw std::runtime_error("Failed to create table");
        }
        doInsert = true;
    }
    if (doInsert || (query.next() && query.value(0).toInt() < 1)) {
        if (!query.exec("insert into LastKnownState values (0)")) {
            logFatal() << "Failed to insert row" << query.lastError().text();
            throw std::runtime_error("Failed to insert row");
        }
        if (!query.exec("create table IBD (busy INTEGER)")) {
            logFatal() << "Failed to create notificatoin table IBD" << query.lastError().text();
            throw std::runtime_error("Failed to create table");
        }
        if (!query.exec("insert into IBD values (42)")) {
            logFatal() << "Failed...";
            throw std::runtime_error("Failed...");
        }
    }
}

void AddressIndexer::run()
{
    assert(m_dataSource);
    while (!isInterruptionRequested()) {
        Message message = m_dataSource->nextBlock(blockheight() + 1, m_uncommittedData.empty() ? ULONG_MAX : 20000);

        if (m_flushRequested.load() == 1) {
            commitAllData();
            m_flushRequested = 0;
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
            } else if (parser.tag() == Api::BlockChain::Tx_Out_ScriptHash) {
                assert(parser.dataLength() == 32);
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
    Q_ASSERT(QThread::currentThread() == this);
    if (m_height == -1) {
        Q_ASSERT(m_uncommittedData.empty());
        return;
    }

    QTime time;
    time.start();
    int rowsInserted = 0;
    logCritical() << "AddressDB sending data to SQL DB";
    // create tables outside of transaction
    for (size_t db = 0; db < m_uncommittedData.size(); ++db) {
        const std::deque<Entry> &list = m_uncommittedData.at(db);
        if (!list.empty()) {
            const QString table = addressTable(db);
            QSqlQuery query(m_insertDb);
            if (!m_spec->queryTableExists(query, table)) {
                static QString q("create table %1 ("
                          "address_row INTEGER, "
                          "block_height INTEGER, offset_in_block INTEGER, out_index INTEGER)");
                if (!query.exec(q.arg(table))) {
                    logFatal() << "Failed to create table" << query.lastError().text();
                    QCoreApplication::exit(1);
                }
                // when creating a new table, set the index on the previous table.
                if (db > 0 && m_topOfChain.load() == InitialSyncFinished) {
                    if (!m_spec->createIndexIfNotExists(query, addressTable(db - 1))) {
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
    logCritical().nospace() << "AddressDB: SQL-DB took " << time.elapsed() << "ms to insert " << rowsInserted << " rows";

    if (m_topOfChain == FlushRequested) { // only ever run this code once per DB
        logCritical() << "Reached top of chain, creating indexes on our tables";
        for (int db = 0;; ++db) {
            if (!m_spec->queryTableExists(query, addressTable(db))) // found the last DB
                break;
            const QString tableName = addressTable(db);
            if (m_spec->createIndexIfNotExists(query, tableName))
                logInfo() << "Created index on SQL table" << tableName;
        }
        logCritical() << "Dropping table 'IBD' which was our indicator of initial sync";
        query.exec("drop table IBD");
        m_topOfChain = InitialSyncFinished;
    }
}
