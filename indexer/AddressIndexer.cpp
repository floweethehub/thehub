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
#include <uint256.h>

#include <QTime>
#include <qsettings.h>
#include <qsqlerror.h>
#include <qtimer.h>
#include <qvariant.h>
#include <qcoreapplication.h>

#include <boost/filesystem.hpp>
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

AddressIndexer::AddressIndexer(const boost::filesystem::path &basedir)
    : m_addresses(basedir),
      m_basedir(QString::fromStdWString(basedir.wstring()))
{
}

void AddressIndexer::loadSetting(const QSettings &settings)
{
    auto db = valueFromSettings(settings, "db_driver");
    m_db = QSqlDatabase::addDatabase(db);
    if (!m_db.isValid()) {
        if (QSqlDatabase::drivers().contains(db)) {
            logFatal().nospace() << "Failed to open a databse (" << db << "), missing libs?";
        } else {
            logFatal() << "The configured database is not known. Please select from this list:";
            logFatal() << QSqlDatabase::drivers().toStdList();
        }
        logCritical() << "Error reported:" << m_db.lastError().text();
        throw std::runtime_error("Failed to read database");
    }

    if (db == "QPSQL") {
        m_db.setDatabaseName(valueFromSettings(settings, "db_database"));
        m_db.setUserName(valueFromSettings(settings, "db_username"));
        m_db.setPassword(valueFromSettings(settings, "db_password"));
        m_db.setHostName(valueFromSettings(settings, "db_hostname"));
    } else if (db == "QSQLITE") {
        m_db.setDatabaseName(m_basedir + "/addresses.db");
    }

    if (m_db.isValid() && m_db.open()) {
        createTables();
    } else {
        logFatal() << "Failed opening the database-connection" << m_db.lastError().text();
        throw std::runtime_error("Failed to open database connection");
    }
    Q_ASSERT(m_dirtyData == nullptr);
    QTimer::singleShot(0, this, SLOT(createNewDirtyData())); // do this on the proper thread
}

int AddressIndexer::blockheight()
{
    if (m_lastKnownHeight == -1) {
        QSqlQuery query(m_db);
        query.prepare("select blockHeight from LastKnownState");
        if (!query.exec()) {
            logFatal() << "Failed to select" << query.lastError().text();
            QCoreApplication::exit(1);
        }
        query.next();
        m_lastKnownHeight = query.value(0).toInt();
    }
    return m_lastKnownHeight;
}

void AddressIndexer::blockFinished(int blockheight, const uint256 &)
{
    DirtyData *dd = m_dirtyData;
    if (++dd->m_uncommittedCount > 150000) {
        dd->setHeight(blockheight);
        connect(dd, SIGNAL(finished(int)), this, SLOT(commitFinished(int)));
        Q_ASSERT(m_isCommitting == false); // if not, then the caller failed to use our isCommitting() output :(
        m_isCommitting = true;
        m_dirtyData = nullptr; // it deletes itself
        QTimer::singleShot(0, this, SLOT(createNewDirtyData())); // do this on the proper thread
        QTimer::singleShot(0, dd, SLOT(commitAllData())); // more work there too
    }
    else {
        m_lastKnownHeight = blockheight;
        emit finishedProcessingBlock();
    }
}

void AddressIndexer::insert(const Streaming::ConstBuffer &addressId, int outputIndex, int blockHeight, int offsetInBlock)
{
    // an address is a hash160
    assert(addressId.size() == 20);

    const uint160 *address = reinterpret_cast<const uint160*>(addressId.begin());
    auto result = m_addresses.lookup(*address);
    if (result.db == -1)
        result = m_addresses.append(*address);
    assert(result.db >= 0);
    assert(result.row >= 0);

    if (result.db >= m_dirtyData->m_uncommittedData.size())
        m_dirtyData->m_uncommittedData.resize(result.db + 1);

    std::deque<DirtyData::Entry> &data = m_dirtyData->m_uncommittedData[result.db];
    assert(outputIndex < 0x7FFF);
    data.push_back({(short) outputIndex, blockHeight, result.row, offsetInBlock});
    ++m_dirtyData->m_uncommittedCount;
}

std::vector<AddressIndexer::TxData> AddressIndexer::find(const uint160 &address) const
{
    std::vector<TxData> answer;
    auto result = m_addresses.lookup(address);
    if (result.db == -1)
        return answer;

    QSqlQuery query(m_db);
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
        txData.blockHeight = (short) query.value(1).toInt();
        txData.outputIndex = (short) query.value(2).toInt();
        answer.push_back(txData);
    }
    return answer;
}

void AddressIndexer::commitFinished(int blockHeight)
{
    m_isCommitting = false;
    if (m_lastKnownHeight < blockHeight) {
        m_lastKnownHeight = blockHeight;
        emit finishedProcessingBlock();
    }
}

void AddressIndexer::createNewDirtyData()
{
    m_dirtyData = new DirtyData(this, &m_db);
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

    QSqlQuery query(m_db);
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
}

bool AddressIndexer::isCommitting() const
{
    return m_isCommitting;
}

void AddressIndexer::flush()
{
    if (m_isCommitting) // we are flushing right now!
        return;
    if (m_dirtyData->m_uncommittedCount == 0)
        return;

    DirtyData *dd = m_dirtyData;
    Q_ASSERT(dd);
    connect(m_dirtyData, SIGNAL(finished(int)), this, SLOT(commitFinished(int)));
    m_isCommitting = true;
    m_dirtyData = nullptr; // it deletes itself
    QTimer::singleShot(0, this, SLOT(createNewDirtyData())); // do this on the proper thread
    QTimer::singleShot(0, dd, SLOT(commitAllData())); // more work there too
}


DirtyData::DirtyData(QObject *parent, QSqlDatabase *db)
    : QObject(parent),
      m_db(db)
{
}

void DirtyData::commitAllData()
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
            QSqlQuery query(*m_db);
            if (!query.exec(QString("select count(*) from ") + table)) {
                static QString q("create table %1 ("
                          "address_row INTEGER, "
                          "block_height INTEGER, offset_in_block INTEGER, out_index INTEGER)");
                if (!query.exec(q.arg(table))) {
                    logFatal() << "Failed to create table" << query.lastError().text();
                    QCoreApplication::exit(1);
                }
            }
        }
    }

    m_db->transaction();
    for (size_t db = 0; db < m_uncommittedData.size(); ++db) {
        const std::deque<Entry> &list = m_uncommittedData.at(db);
        if (!list.empty()) {
            const QString table = addressTable(db);
            QSqlQuery query(*m_db);
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
    QSqlQuery query(*m_db);
    query.prepare("update LastKnownState set blockHeight=:bh");
    query.bindValue(":bh", m_height);
    if (!query.exec()) {
        logFatal() << "Failed to update blockheight" << query.lastError().text();
        logDebug() << " q" << query.lastQuery();
    }

    m_db->commit();
    deleteLater();
    logInfo().nospace() << "AddressDB: SQL-DB took " << time.elapsed() << "ms to insert " << rowsInserted << " rows";
    emit finished(m_height);
}

void DirtyData::setHeight(int height)
{
    m_height = height;
}
