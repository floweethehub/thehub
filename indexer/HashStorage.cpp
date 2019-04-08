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
#include <qfileinfo.h>

#include "HashStorage.h"
#include "HashStorage_p.h"

#include <boost/filesystem.hpp>

#define WIDTH 20

namespace {
QMap<int, int> createResortReversed(const QMap<int, int> &map)
{
    QMap<int, int> answer;
    QMapIterator<int, int> iter(map);
    while (iter.hasNext()) {
        iter.next();
        Q_ASSERT(!answer.contains(iter.value()));
        answer.insert(iter.value(), iter.key());
    }
    return answer;
}

struct Pair {
    Pair(const uint160 *h, int i) : hash(h), index(i) {}
    const uint160 *hash;
    int index;
};

struct PairSorter {
    PairSorter(const QHash<uint160, int> &orig) {
        pairs.reserve(orig.size());
        QHashIterator<uint160, int> iter(orig);
        while (iter.hasNext()) {
            iter.next();
            pairs.push_back(Pair(&iter.key(), iter.value()));
        }
    }

    std::vector<Pair> pairs;

    bool operator()(const Pair &a, const Pair &b) {
        return a.hash->Compare(*b.hash) <= 0;
    }
};
}

uint160 HashStoragePrivate::s_null = uint160();

HashStorage::HashStorage(const boost::filesystem::path &basedir)
    : d(new HashStoragePrivate(basedir))
{
}

HashStorage::~HashStorage()
{
    delete d;
}

int HashStorage::databaseCount() const
{
    return d->dbs.length();
}

HashIndexPoint HashStorage::append(const uint160 &hash)
{
    QList<HashList*> dbs(d->dbs);
    Q_ASSERT(!dbs.isEmpty());
    auto db = dbs.last();
    int index = db->append(hash);
    Q_ASSERT(index >= 0);
    if (db->m_cacheMap.size() > 400000) {
        db->finalize();
        if (db->m_sortedFile.size() > 1300000000) { // > 1.3GB
            QString newFilename = d->basedir + QString("/data-%1").arg(dbs.length() + 1);
            HashList *hl = new HashList(newFilename);

            // technically not reentrant! Just ok-ish
            if (dbs == d->dbs)
                d->dbs.append(hl);
        }
    }
    return HashIndexPoint(dbs.size() - 1, index);
}

const uint160 &HashStorage::find(HashIndexPoint point) const
{
    Q_ASSERT(point.db >= 0);
    Q_ASSERT(point.row >= 0);
    QList<HashList*> dbs(d->dbs);
    if (point.db > dbs.size())
        return HashStoragePrivate::s_null;
    return dbs.at(point.db)->at(point.row);
}

HashIndexPoint HashStorage::lookup(const uint160 &hash) const
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
    d->dbs.last()->finalize();
    d->dbs.append(HashList::createEmpty(d->basedir, d->dbs.size() + 1));
}


// -----------------------------------------------------------------

HashList::HashList(const QString &dbBase)
    : m_filebase(dbBase),
      m_sortedFile(m_filebase + ".db"),
      m_reverseLookupFile(m_filebase + ".index")
{
    QFile info(m_filebase + ".info");
    if (info.open(QIODevice::ReadOnly)) {
        QDataStream in(&info);
        in >> m_nextId;
    }

    // is it finalized?
    if (m_sortedFile.open(QIODevice::ReadOnly)) {
        m_sorted = m_sortedFile.map(0, m_sortedFile.size());
        m_sortedFile.close();
        if (m_reverseLookupFile.open(QIODevice::ReadOnly)) {
            m_reverseLookup = m_reverseLookupFile.map(0, m_reverseLookupFile.size());
            m_reverseLookupFile.close();
        }
    }
    else { // We are not finalized, so we should have a log
        m_log = new QFile(m_filebase + ".log");
        if (!m_log->open(QIODevice::ReadWrite))
            throw std::runtime_error("HashList: failed to open log file");
        while(true) {
            uint160 item;
            auto byteCount = m_log->read(reinterpret_cast<char*>(item.begin()), WIDTH);
            if (byteCount == WIDTH) {
                m_cacheMap.insert(item, m_nextId++);
            } else if (byteCount == 0) {
                break;
            }
        }
    }
}

HashList::~HashList()
{
    if (m_log) {
        m_log->close();
        delete m_log;
    }
    if (m_sorted)
        m_sortedFile.unmap(m_sorted);
    if (m_reverseLookup)
        m_reverseLookupFile.unmap(m_reverseLookup);
}

HashList *HashList::createEmpty(const QString &dbBase, int index)
{
    QString name("/data-%1");
    HashList *answer = new HashList(dbBase + name.arg(index, 3, 10, QChar('0')));
    return answer;
}

int HashList::append(const uint160 &hash)
{
    QMutexLocker lock(&m_mutex);
    const int id = m_nextId++;
    m_cacheMap.insert(hash, id);
    Q_ASSERT(m_log);
    m_log->write(reinterpret_cast<const char*>(hash.begin()), WIDTH);

    return id;
}

int HashList::find(const uint160 &hash) const
{
    QMutexLocker lock(&m_mutex);
    auto item = m_cacheMap.find(hash);
    if (item != m_cacheMap.end())
        return item.value();

    int pos = 0;
    int endpos = m_sortedFile.size() / WIDTH - 1;
    while (pos <= endpos) {
        int m = (pos + endpos) / 2;
        uint160 *item = (uint160*)(m_sorted + m * (WIDTH + sizeof(int)));
        int comp = item->Compare(hash);
        if (comp < 0)
            pos = m + 1;
        else if (comp > 0)
            endpos = m - 1;
        else
            return *reinterpret_cast<int*>(m_sorted + m * (WIDTH + sizeof(int)) + WIDTH);
    }

    return -1;
}

const uint160 &HashList::at(int index) const
{
    Q_ASSERT(index >= 0);
    QMutexLocker lock(&m_mutex);
    if (m_reverseLookup) {
        if (m_reverseLookupFile.size() / sizeof(int) < index)
            throw std::runtime_error("row out of bounds");

        // map index to row
        int row = *reinterpret_cast<int*>(m_reverseLookup + index * sizeof(int));
        const uint160 *dummy = reinterpret_cast<uint160*>(m_sorted + row * (WIDTH + sizeof(int)));
        return *dummy;
    }

    // also check the dirty cache. Do this at end as this is a slow lookup
    QHashIterator<uint160, int> iter(m_cacheMap);
    if (iter.findNext(index)) {
        return iter.key();
    }
    return HashStoragePrivate::s_null;
}

void HashList::finalize()
{
    Q_ASSERT(!m_sortedFile.exists());
    Q_ASSERT(!m_sorted);
    Q_ASSERT(!m_reverseLookup);
    QMutexLocker lock(&m_mutex);
    if (!m_sortedFile.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        throw std::runtime_error("Failed to open db file for writing");
    }
    if (!m_reverseLookupFile.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        throw std::runtime_error("Failed to open index file for writing");
    }

    PairSorter sorted(m_cacheMap);
    std::sort(sorted.pairs.begin(), sorted.pairs.end(), sorted);
    QMap<int, int> lookupTable;
    for (auto iter = sorted.pairs.begin(); iter != sorted.pairs.end(); ++iter) {
        assert(iter->index >= 0); // no negative numbers, please.
        m_sortedFile.write(reinterpret_cast<const char*>(iter->hash->begin()), WIDTH);
        m_sortedFile.write(reinterpret_cast<const char*>(&iter->index), sizeof(int));
        lookupTable.insert(iter->index, lookupTable.size());
    }
    sorted.pairs.clear();
    m_cacheMap.clear();
    m_sortedFile.close();
    for (auto iter = lookupTable.begin(); iter != lookupTable.end(); ++iter) {
        m_reverseLookupFile.write(reinterpret_cast<const char*>(&iter.value()), sizeof(int));
    }
    lookupTable.clear();
    m_reverseLookupFile.close();
    m_log->close();
    bool ok = m_log->remove();
    Q_ASSERT(ok); Q_UNUSED(ok);
    delete m_log;
    m_log = nullptr;

    if (m_sortedFile.open(QIODevice::ReadOnly)) {
        m_sorted = m_sortedFile.map(0, m_sortedFile.size());
        m_sortedFile.close();
    }
    if (m_reverseLookupFile.open(QIODevice::ReadOnly)) {
        m_reverseLookup = m_reverseLookupFile.map(0, m_reverseLookupFile.size());
        m_reverseLookupFile.close();
    }
}


// -----------------------------------------------------------------

HashStoragePrivate::HashStoragePrivate(const boost::filesystem::path &basedir_)
    : basedir(QString::fromStdWString(basedir_.wstring()))
{
    boost::filesystem::create_directories(basedir_);
    int index = 1;
    const QString fileBase = QString("%1/data-%2").arg(basedir);
    while (true) {
        QString dbFilename = fileBase.arg(index, 3, 10, QChar('0'));
        QFileInfo dbInfo(dbFilename + ".db");
        if (!dbInfo.exists()) {
            // not finalized yet only has a log
            QFileInfo dbLog(dbFilename + ".log");
            if (!dbLog.exists())
                break;
        }
        dbs.append(new HashList(dbFilename));
        ++index;
    }
    if (dbs.isEmpty()) {
        dbs.append(HashList::createEmpty(basedir, 1));
    }
}

HashStoragePrivate::~HashStoragePrivate()
{
    qDeleteAll(dbs);
    dbs.clear();
}
