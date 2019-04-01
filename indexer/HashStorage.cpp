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
#include <QDataStream>

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

void HashStorage::finalize()
{
    QList<HashList*> dbs(d->dbs);
    for (auto db : dbs) {
        db->finalize();
    }
}


// -----------------------------------------------------------------

HashList::HashList(const boost::filesystem::path &dbBase)
{
    m_filebase = QString::fromStdWString(dbBase.wstring());
    m_log = new QFile(m_filebase + ".log");
    if (!m_log->open(QIODevice::ReadWrite))
        throw std::runtime_error("HashList: failed to open log file");

    m_sortedFile = new QFile(m_filebase + ".db");
    if (m_sortedFile->open(QIODevice::ReadOnly)) {
        m_sorted = m_sortedFile->map(0, m_sortedFile->size());
        m_sortedFile->close();
    }

    QFile info(m_filebase + ".info");
    if (info.open(QIODevice::ReadOnly)) {
        QDataStream in(&info);
        in >> m_nextId;
        in >> m_jumptables;
        in >> m_resortMap;
    }

    while(true) {
        uint256 item;
        auto byteCount = m_log->read(reinterpret_cast<char*>(item.begin()), 32);
        if (byteCount == 32) {
            m_cacheMap.insert(m_nextId++, item);
        } else if (byteCount == 0) {
            break;
        }
    }
}

HashList::~HashList()
{
    m_log->close();
    delete m_log;
    if (m_sorted)
        m_sortedFile->unmap(m_sorted);
    delete m_sortedFile;
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

    // TODO binary search
    /*
     function binary_search(A, n, T):
        L := 0
        R := n − 1
        while L <= R:
            m := floor((L + R) / 2)
            if A[m] < T:
                L := m + 1
            else if A[m] > T:
                R := m - 1
            else:
                return m
        return unsuccessful
    */
    const int rowCount = m_sortedFile->size() / 32;
    for (int row = 0; row < rowCount; ++row) {
        uint256 *dummy = (uint256*)(m_sorted + row * 32);
        if (dummy->Compare(hash) == 0)
            return row;
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
    auto resortIter = m_resortMap.find(row);
    if (resortIter != m_resortMap.end()) {
        row = resortIter.value();
    }
    uint256 *dummy = (uint256*)(m_sorted + row * 32);
    return *dummy;
}

namespace {
struct SortCacheHelper {
    QMap<int, uint256> cacheMap;
    bool operator() (int i, int j) {
        return cacheMap.value(i).Compare(cacheMap.value(j)) <= 0;
    }
};

}

void HashList::finalize()
{
    QMutexLocker lock(&m_mutex);
    QList<int> sortedKeys = m_cacheMap.keys();
    {
        SortCacheHelper helper { m_cacheMap };
        std::sort(sortedKeys.begin(), sortedKeys.end(), helper);
    }
    const QString tmpFile = m_sortedFile->fileName() + '~';
    QFile newFile(tmpFile);
    if (!newFile.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        throw std::runtime_error("Failed to open tmp file for writing");
    }
    m_jumptables.clear();
    m_jumptables.resize(256);
    int row = 0;
    int oldRow = 0;
    QMap<int, int> resortMap(m_resortMap);
    m_resortMap.clear();
    const int maxOldRow = m_sortedFile->size() / 32;
    for (auto key : sortedKeys) {
        const uint256 &hash = m_cacheMap.value(key);

        while (oldRow < maxOldRow) {
            const char *rowLocation = reinterpret_cast<char*>(m_sorted) + oldRow * 32;
            if (((uint256*)rowLocation)->Compare(hash) > 0)
                break;

            newFile.write(rowLocation, 32);

            QMapIterator<int,int> mi(resortMap);
            bool ok = mi.findNext(oldRow++);
            Q_ASSERT(ok); Q_UNUSED(ok);
            m_resortMap.insert(mi.key(), row++);
            resortMap.remove(mi.key());
        }

        newFile.write(reinterpret_cast<const char*>(hash.begin()), 32);
        m_resortMap.insert(key, row++);
    }
    while (oldRow < maxOldRow) {
        newFile.write(reinterpret_cast<char*>(m_sorted) + oldRow * 32, 32);

        QMapIterator<int,int> mi(resortMap);
        bool ok = mi.findNext(oldRow++);
        Q_ASSERT(ok); Q_UNUSED(ok);
        m_resortMap.insert(mi.key(), row++);
        resortMap.remove(mi.key());
    }
    m_nextId = row; // do we need this?
    if (m_sorted)
        m_sortedFile->unmap(m_sorted);
    m_sorted = nullptr;
    m_log->close();
    newFile.close();
    m_sortedFile->remove();
    if (!newFile.rename(m_filebase + ".db")) {
        // TODO
        return;
    }
    m_cacheMap.clear();
    m_log->open(QIODevice::Truncate | QIODevice::WriteOnly);
    if (m_sortedFile->open(QIODevice::ReadOnly)) {
        m_sorted = m_sortedFile->map(0, m_sortedFile->size());
        m_sortedFile->close();
    }

    QFile info(m_filebase + ".info");
    if (info.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QDataStream out(&info);
        out << m_nextId;
        out << m_jumptables;
        out << m_resortMap;
    }
}


// -----------------------------------------------------------------

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
