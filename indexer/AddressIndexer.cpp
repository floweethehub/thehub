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

#include <qsqlerror.h>
#include <qvariant.h>

#include <boost/filesystem.hpp>

AddressIndexer::AddressIndexer(const boost::filesystem::path &basedir)
    : m_addresses(basedir),
    m_db( QSqlDatabase::addDatabase("QSQLITE")),
    m_insertQuery(m_db),
    m_lastBlockHeightQuery(m_db)
{
    if (!m_db.isValid()) {
        logFatal() << "Failed to open a SQLITE databse, missing Qt plugin?";
        logCritical() << m_db.lastError().text();
        throw std::runtime_error("Failed to read database");
    }

    boost::filesystem::create_directories(basedir);
    QString dbFile = QString::fromStdWString(basedir.wstring()) + "/addresses.db";
    m_db.setDatabaseName(dbFile);
    if (m_db.isValid() && m_db.open()) {
        createTables();
    } else {
        logFatal() << "Failed opening the database-connection" << m_db.lastError().text();
        throw std::runtime_error("Failed top en database connection");
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
        logFatal() << "Failed to insert addressusage" << m_lastBlockHeightQuery.lastError().text();
        throw std::runtime_error("Failed to update");
    }
    m_lastKnownHeight = blockheight;
}

void AddressIndexer::insert(const Streaming::ConstBuffer &addressId, int outputIndex, int blockHeight, int offsetInBlock)
{
    // an address is a hash160
    assert(addressId.size() == 20);

    const uint160 *address = reinterpret_cast<const uint160*>(addressId.begin());
    auto result = m_addresses.find(*address);
    if (result.db == -1)
        result = m_addresses.append(*address);
    assert(result.db >= 0);
    assert(result.row >= 0);

    m_insertQuery.bindValue(":db", result.db);
    m_insertQuery.bindValue(":row", result.row);
    m_insertQuery.bindValue(":bh", blockHeight);
    m_insertQuery.bindValue(":oib", offsetInBlock);
    m_insertQuery.bindValue(":idx", outputIndex);
    if (!m_insertQuery.exec()) {
        logFatal() << "Failed to insert addressusage" << m_insertQuery.lastError().text();
        logDebug() << m_insertQuery.lastQuery();
        throw std::runtime_error("Failed to insert");
    }
}

std::vector<AddressIndexer::TxData> AddressIndexer::find(const uint160 &address) const
{
    std::vector<TxData> answer;
    auto result = m_addresses.find(address);
    if (result.db == -1)
        return answer;

    QSqlQuery query(m_db);
    query.prepare("select offset_in_block, block_height, out_index "
                  "FROM AddressUsage WHERE "
                  "address_db=:db AND address_row=:row");
    query.bindValue(":db", result.db);
    query.bindValue(":row", result.row);
    if (!query.exec()) {
        logFatal() << "Failed to select" << query.lastError().text();
        throw std::runtime_error("Failed to create table");
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
     * AddressUsage
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
    if (!query.exec("select count(*) from AddressUsage")) {
        QString q("create table AddressUsage ("
                  "address_db INTEGER, address_row INTEGER, "
                  "block_height INTEGER, offset_in_block INTEGER, out_index INTEGER)");
        if (!query.exec(q)) {
            logFatal() << "Failed to create table" << query.lastError().text();
            throw std::runtime_error("Failed to create table");
        }
    }
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
    m_insertQuery.prepare("insert into AddressUsage (address_db, address_row, "
       "block_height, offset_in_block, out_index) VALUES ("
                          ":db, :row, :bh, :oib, :idx)");

    m_lastBlockHeightQuery.prepare("update LastKnownState set blockHeight=:bh");
}