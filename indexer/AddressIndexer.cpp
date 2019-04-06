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

#include <qsettings.h>
#include <qsqlerror.h>
#include <qvariant.h>

#include <boost/filesystem.hpp>
namespace {
QString valueFromSettings(const QSettings &settings, const QString &key) {
    QVariant x = settings.value(QString("addressdb/") + key);
    if (x.isNull())
        x = settings.value(key);
    return x.toString();
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
}

int AddressIndexer::blockheight()
{
    if (m_lastKnownHeight == -1) {
        QSqlQuery query(m_db);
        query.prepare("select blockHeight from LastKnownState");
        if (!query.exec()) {
            logFatal() << "Failed to select" << query.lastError().text();
            throw std::runtime_error("Failed to select");
        }
        query.next();
        m_lastKnownHeight = query.value(0).toInt();
    }
    return m_lastKnownHeight;
}

void AddressIndexer::blockFinished(int blockheight, const uint256 &)
{
    m_lastBlockHeightQuery.bindValue(":bh", blockheight);
    if (!m_lastBlockHeightQuery.exec()) {
        logFatal() << "Failed to update blockheight" << m_lastBlockHeightQuery.lastError().text();
        logDebug() << " q" << m_lastBlockHeightQuery.lastQuery();
        throw std::runtime_error("Failed to update");
    }
    m_lastKnownHeight = blockheight;
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
    while (result.db >= m_insertQuery.size()) {
        const QString table = QString("AddressUsage%1").arg(qlonglong(m_insertQuery.size()), 2, 10, QChar('_'));
        QSqlQuery query(m_db);
        if (!query.exec(QString("select count(*) from ") + table)) {
            static QString q("create table %1 ("
                      "address_row INTEGER, "
                      "block_height INTEGER, offset_in_block INTEGER, out_index INTEGER)");
            if (!query.exec(q.arg(table))) {
                logFatal() << "Failed to create table" << query.lastError().text();
                throw std::runtime_error("Failed to create table");
            }
        }
        m_insertQuery.append(QSqlQuery(m_db));
        m_insertQuery.last().prepare(QString("insert into ") + table
              + " (address_row, block_height, offset_in_block, out_index) VALUES ("
                              ":row, :bh, :oib, :idx)");
    }

    QSqlQuery &q = m_insertQuery[result.db];
    q.bindValue(":row", result.row);
    q.bindValue(":bh", blockHeight);
    q.bindValue(":oib", offsetInBlock);
    q.bindValue(":idx", outputIndex);
    if (!q.exec()) {
        logFatal() << "Failed to insert AddressUsage_" << result.db << q.lastError().text();
        throw std::runtime_error("Failed to insert");
    }
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
        throw std::runtime_error("Failed to select");
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

void AddressIndexer::createTables()
{
    /* Tables
     * AddressUsage_N
     *  address_db      INTEGER   (the db that the hashStorage provided us with)
     *  address_row     INTEGER   (the row that the hashStorage provided us with)
     *  block_height    INTEGER    \
     *  offset_in_block INTEGER    /-- together give the transaction
     *  out_index       INTEGER
     *
     * LastKnownState
     *  blockHeight    INTEGER
     */

    QSqlQuery query(m_db);
    if (!query.exec("select count(*) from LastKnownState")) {
        QString q("create table LastKnownState ("
                  "blockHeight INTEGER)");
        if (!query.exec(q)) {
            logFatal() << "Failed to create table" << query.lastError().text();
            throw std::runtime_error("Failed to create table");
        }
        if (!query.exec("insert into LastKnownState values (0)")) {
            logFatal() << "Failed to insert row" << query.lastError().text();
            throw std::runtime_error("Failed to insert row");
        }
    }

    m_lastBlockHeightQuery = QSqlQuery(m_db);
    m_lastBlockHeightQuery.prepare("update LastKnownState set blockHeight=:bh");
}
