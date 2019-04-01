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

#include <QString>
#include <Logger.h>

#include "HashStorage.h"
#include "HashStorage_p.h"

uint256 HashStoragePrivate::s_null = uint256();

HashStorage::HashStorage(const boost::filesystem::path &basedir)
    : d(new HashStoragePrivate(basedir))
{
}

HashStorage::~HashStorage()
{
    delete d;
}

HashIndexPoint HashStorage::append(const uint256 &hash)
{
    QList<HashList*> dbs(d->dbs);
    Q_ASSERT(!dbs.isEmpty());
    int index = dbs.last()->append(hash);
    Q_ASSERT(index >= 0);
    // TODO check if db is full etc.
    return HashIndexPoint(dbs.size() - 1, index);
}

const uint256 &HashStorage::at(HashIndexPoint point) const
{
    Q_ASSERT(point.db >= 0);
    Q_ASSERT(point.row >= 0);
    QList<HashList*> dbs(d->dbs);
    if (point.db > dbs.size())
        return HashStoragePrivate::s_null;
    return dbs.at(point.db)->at(point.row);
}

HashIndexPoint HashStorage::find(const uint256 &hash) const
{
    QList<HashList*> dbs(d->dbs);
    Q_ASSERT(!dbs.isEmpty());
    for (int i = 0; i < dbs.length(); ++i) {
        int result = dbs.at(i)->find(hash);
        if (result >= 0) {
            return HashIndexPoint(i, result);
        }
    }
    return HashIndexPoint();
}



HashList::HashList(const boost::filesystem::path &dbBase)
{
    // TODO load
    m_log = new QFile(QString::fromStdWString(dbBase.wstring()) + ".log");
    if (!m_log->open(QIODevice::ReadWrite))
        throw std::runtime_error("HashList: failed to open log file");

    int id = 0; // TODO get this offset from the info file.
    while(true) {
        uint256 item;
        auto byteCount = m_log->read(reinterpret_cast<char*>(item.begin()), 32);
        if (byteCount == 32) {
            m_cacheMap.insert(id++, item);
        } else if (byteCount == 0) {
            break;
        }
    }
    m_nextId = id;
}

HashList::~HashList()
{
    // TODO sync/save
    m_log->close();
    delete m_log;
}

int HashList::append(const uint256 &hash)
{
    QMutexLocker lock(&m_mutex);
    const int id = m_nextId++;
    m_cacheMap.insert(id, hash);
    m_log->write(reinterpret_cast<const char*>(hash.begin()), 32);

    return id;
}

int HashList::find(const uint256 &hash)
{
    QMutexLocker lock(&m_mutex);
    QMapIterator<int, uint256> iter(m_cacheMap);
    if (iter.findNext(hash)) {
        return iter.key();
    }

    return -1;
}

const uint256 &HashList::at(int row) const
{
    QMutexLocker lock(&m_mutex);
    auto iter = m_cacheMap.find(row);
    if (iter != m_cacheMap.end()) {
        return iter.value();
    }

    return HashStoragePrivate::s_null;
}


HashStoragePrivate::HashStoragePrivate(const boost::filesystem::path &basedir)
    : basedir(basedir)
{
    dbs.append(new HashList(basedir / "data-1"));
}

HashStoragePrivate::~HashStoragePrivate()
{
    qDeleteAll(dbs);
    dbs.clear();
}
