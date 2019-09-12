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
#ifndef ADDRESSINDEXER_H
#define ADDRESSINDEXER_H

#include <QList>
#include <QThread>
#include <qsqldatabase.h>
#include <qsqlquery.h>
#include <streaming/ConstBuffer.h>

#include "HashStorage.h"

#include <boost/filesystem/path.hpp>

class QSettings;
class DirtyData;
class Indexer;

class TableSpecification;

class AddressIndexer : public QThread
{
    Q_OBJECT
public:
    AddressIndexer(const boost::filesystem::path &basedir, Indexer *datasource);
    ~AddressIndexer() override;
    void loadSetting(const QSettings &settings);

    int blockheight();
    void blockFinished(int blockheight, const uint256 &blockId);
    void insert(const Streaming::ConstBuffer &addressId, int outputIndex, int blockHeight, int offsetInBlock);

    void reachedTopOfChain();

    struct TxData {
        int offsetInBlock = 0;
        int blockHeight = -1;
        short outputIndex = -1;
    };

    std::vector<TxData> find(const uint160 &address) const;

    void run() override;

private:
    void createTables();
    void commitAllData();

    struct Entry {
        short outIndex;
        int height, row, offsetInBlock;
    };
    std::vector<std::deque<Entry> > m_uncommittedData;
    int m_uncommittedCount = 0;
    int m_height = -1;

    HashStorage m_addresses;
    QString m_basedir;
    Indexer *m_dataSource;

    QSqlDatabase m_insertDb;
    QSqlDatabase m_selectDb;
    QList<QSqlQuery> m_insertQuery;
    QAtomicInt m_flushRequested;
    enum TopOfChain {
        InInitialSync = 0,
        InitialSyncFinished = 1,
        FlushRequested = 2
    };
    QAtomicInt m_topOfChain;

    TableSpecification *m_spec = nullptr;
};

#endif
