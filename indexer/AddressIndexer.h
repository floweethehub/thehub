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
#include <qobject.h>
#include <qsqldatabase.h>
#include <qsqlquery.h>
#include <streaming/ConstBuffer.h>

#include "HashStorage.h"

class QSettings;
class DirtyData;

class AddressIndexer : public QObject
{
    Q_OBJECT
public:
    AddressIndexer(const boost::filesystem::path &basedir);
    void loadSetting(const QSettings &settings);

    int blockheight();
    void blockFinished(int blockheight, const uint256 &blockId);
    void insert(const Streaming::ConstBuffer &addressId, int outputIndex, int blockHeight, int offsetInBlock);

    struct TxData {
        int offsetInBlock = 0;
        short blockHeight = -1;
        short outputIndex = -1;
    };

    std::vector<TxData> find(const uint160 &address) const;

    bool isCommitting() const;

signals:
    void finishedProcessingBlock();

private slots:
    void commitFinished(int blockHeight);
    void createNewDirtyData();

private:
    void createTables();

    DirtyData *m_dirtyData = nullptr;


    HashStorage m_addresses;
    QString m_basedir;

    QSqlDatabase m_db;
    QList<QSqlQuery> m_insertQuery;

    int m_lastKnownHeight = -1;
    bool m_isCommitting = false; // if there is DirtyData running in another thread committing stuff
};

class DirtyData : public QObject {
    Q_OBJECT
public:
    DirtyData(QObject *parent, QSqlDatabase *db);

    struct Entry {
        short outIndex;
        int height, row, offsetInBlock;
    };
    std::vector<std::deque<Entry> > m_uncommittedData;
    int m_uncommittedCount = 0;

    void setHeight(int height);

public slots:
    void commitAllData();

signals:
    void finished(int height);

private:
    int m_height;
    QSqlDatabase *m_db;
};

#endif
