/*
 * This file is part of the Flowee project
 * Copyright (C) 2019-2021 Tom Zander <tom@flowee.org>
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
#include <QMapIterator>

#include "HashStorage.h"
#include "HashStorage_p.h"

#include <boost/filesystem.hpp>
#include <unordered_map>

#define WIDTH 32

namespace {
struct Pair {
    Pair(const uint256 *h, int i) : hash(h), index(i) {}
    const uint256 *hash;
    int index;
};

struct PairSorter {
    PairSorter(const std::unordered_map<uint256, int, HashShortener, HashComparison> &orig) {
        pairs.reserve(orig.size());
        for (auto iter = orig.begin(); iter != orig.end(); ++iter) {
            pairs.push_back(Pair(&iter->first, iter->second));
        }
    }

    std::vector<Pair> pairs;
};

bool sortPairs(const Pair &a, const Pair &b) {
    return a.hash->Compare(*b.hash) <= 0;
}

struct PartHashTip {
    int partIndex;
    int value;
    const uint256 *key;
};
struct HashListPartProxy {
    const uchar *file;
    int pos, rows;
};

struct HashCollector {
    HashCollector(const QList<HashListPart*> &parts)
    {
        m_tips.reserve(parts.size());
        m_parts.reserve(parts.size());
        for (int i = 0; i < parts.size(); ++i) {
            auto &p = parts.at(i);
            m_parts.push_back({p->sorted, 0, (int)(p->sortedFileSize / (WIDTH + sizeof(int)))});
            sortInTip(i);
        }
    }

    void sortInTip(int partIndex) {
        PartHashTip tip;
        tip.partIndex = partIndex;
        Q_ASSERT(int(m_parts.size()) > partIndex);
        HashListPartProxy &p = m_parts.at(partIndex);
        Q_ASSERT(p.pos < p.rows);
        const uchar *recordStart = (p.file + p.pos * (WIDTH + sizeof(int)));
        tip.key = reinterpret_cast<const uint256*>(recordStart);
        tip.value = *reinterpret_cast<const int*>(recordStart + WIDTH);
        ++p.pos;

        if (m_tips.empty()) {
            m_tips.append(tip);
            return;
        }
        int l = 0;
        int r = m_tips.size() - 1;
        while (l <= r) {
            int m = (l + r) / 2;
            int comp = m_tips.at(m).key->Compare(*tip.key);
            if (comp < 0)
                l = m + 1;
            else if (comp > 0)
                r = m - 1;
            else {
                assert(false);
                throw std::runtime_error("Duplicate entries in HashStorage");
            }
        }
        m_tips.insert(l, tip);

        // logFatal()  << "ran a sortInTip";
        // for (int i = 0; i < m_tips.size(); ++i) {
        //     logFatal() << *m_tips.at(i).key;
        // }
    }

    void writeHashesToFile(QFile *outFile) {
        Q_ASSERT(outFile);
        Q_ASSERT(outFile->isOpen());
        while (!m_tips.isEmpty()) {
            auto item = m_tips.takeFirst();
            outFile->write(reinterpret_cast<const char*>(item.key->begin()), WIDTH);
            outFile->write(reinterpret_cast<const char*>(&item.value), sizeof(int));

            auto &p = m_parts.at(item.partIndex);
            m_revertLookup.insert(item.value, m_revertLookup.size());
            if (p.pos < p.rows)
                sortInTip(item.partIndex);
        }
        outFile->close();
    }
    void writeRevertLookup(QFile *outFile) {
        Q_ASSERT(outFile);
        Q_ASSERT(outFile->isOpen());
        for (auto iter = m_revertLookup.begin(); iter != m_revertLookup.end(); ++iter) {
            outFile->write(reinterpret_cast<const char*>(&iter.value()), sizeof(int));
        }
        m_revertLookup.clear();
        outFile->close();
    }

private:
    QList<PartHashTip> m_tips;
    std::vector<HashListPartProxy> m_parts;
    QMap<int, int> m_revertLookup;
};

}

uint256 HashStoragePrivate::s_null = uint256();

// -----------------------------------------------------------------

HashListPart::HashListPart(const QString &partBase)
    : sortedFile(partBase + ".db"),
    reverseLookupFile(partBase + ".index")
{
}

HashListPart::~HashListPart()
{
    closeFiles();
}

void HashListPart::openFiles()
{
    Q_ASSERT(sorted == nullptr);
    Q_ASSERT(reverseLookup == nullptr);
    if (sortedFile.open(QIODevice::ReadOnly)) {
        sortedFileSize = sortedFile.size();
        sorted = sortedFile.map(0, sortedFileSize);
        sortedFile.close();
    }
    if (reverseLookupFile.open(QIODevice::ReadOnly)) {
        reverseLookup = reverseLookupFile.map(0, reverseLookupFile.size());
        reverseLookupFile.close();
    }
}

void HashListPart::closeFiles()
{
    // technically speaking, the files are not actually open. They are just mapped.
    if (sorted) {
        sortedFile.unmap(sorted);
        sorted = nullptr;
    }
    if (reverseLookup) {
        reverseLookupFile.unmap(reverseLookup);
        reverseLookup = nullptr;
    }
}


// -----------------------------------------------------------------

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

HashIndexPoint HashStorage::append(const uint256 &hash)
{
    QList<HashList*> dbs(d->dbs);
    Q_ASSERT(!dbs.isEmpty());
    auto db = dbs.last();
    int index = db->append(hash);
    Q_ASSERT(index >= 0);

    // Because we are going to memmap the total file we aim to have a file
    // that is approx (but absolutely not one byte over) a power of two.
    // Lets aim for 0xFFFFFFF (256MB)
    // for similar reasons we have 8 parts with an equal share of the whole each
    if (db->m_cacheMap.size() > 932064)
        db->stabilize();
    else if (db->m_parts.size() > 7 && dbs == d->dbs)
        finalize();
    return HashIndexPoint(dbs.size() - 1, index);
}

const uint256 &HashStorage::find(HashIndexPoint point) const
{
    Q_ASSERT(point.db >= 0);
    Q_ASSERT(point.row >= 0);
    QList<HashList*> dbs(d->dbs);
    if (point.db > dbs.size())
        return HashStoragePrivate::s_null;
    return dbs.at(point.db)->at(point.row);
}

HashIndexPoint HashStorage::lookup(const uint256 &hash) const
{
    QList<HashList*> dbs(d->dbs);
    Q_ASSERT(!dbs.isEmpty());
    for (int i = dbs.length() - 1; i >= 0; --i) {
        int result = dbs.at(i)->lookup(hash);
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
    memset(m_offsets, 0, sizeof(uint32_t) * 256);
    QFile info(m_filebase + ".info");
    int partCount = 0;
    if (info.open(QIODevice::ReadOnly)) {
        QDataStream in(&info);
        in >> m_nextId;
        in >> partCount;
        // an older file-format did not have the 'offsets' yet.
        if (!in.atEnd()) {
            for (int i = 0; i < 256; ++i) {
                in >> m_offsets[i];
            }
        }
    }
    info.close();

    // is it finalized?
    if (m_sortedFile.open(QIODevice::ReadOnly)) {
        Q_ASSERT(partCount == 0);
        m_sortedFileSize = m_sortedFile.size();
        m_sorted = m_sortedFile.map(0, m_sortedFileSize);
        m_sortedFile.close();
        if (m_reverseLookupFile.open(QIODevice::ReadOnly)) {
            m_reverseLookup = m_reverseLookupFile.map(0, m_reverseLookupFile.size());
            m_reverseLookupFile.close();
        }

        if (m_offsets[100] == 0) {
            logCritical() << "Upgrading hashlist to have a jumptable" << m_filebase;
            fillOffsetsTable();
            writeInfoFile();
        }
    }
    else { // We are not finalized, so we should have a log
        m_log = new QFile(m_filebase + ".log");
        if (!m_log->open(QIODevice::ReadWrite))
            throw std::runtime_error("HashList: failed to open log file");
        while(true) {
            uint256 item;
            auto byteCount = m_log->read(reinterpret_cast<char*>(item.begin()), WIDTH);
            if (byteCount == WIDTH) {
                m_cacheMap.insert(std::make_pair(item, m_nextId++));
            } else if (byteCount == 0) {
                break;
            }
        }
        m_parts.reserve(partCount);
        for (int i = 0; i < partCount; ++i) {
            m_parts.append(new HashListPart(QString("%1_%2").arg(m_filebase)
                                  .arg(i, 2, 10, QChar('0'))));
            m_parts.last()->openFiles();
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
    QString name("/hashlist-%1");
    HashList *answer = new HashList(dbBase + name.arg(index, 3, 10, QChar('0')));
    return answer;
}

int HashList::append(const uint256 &hash)
{
    QMutexLocker lock(&m_mutex);
    const int id = m_nextId++;
    m_cacheMap.insert(std::make_pair(hash, id));
    Q_ASSERT(m_log);
    m_log->write(reinterpret_cast<const char*>(hash.begin()), WIDTH);

    return id;
}

int HashList::lookup(const uint256 &hash) const
{
    QMutexLocker lock(&m_mutex);
    auto item = m_cacheMap.find(hash);
    if (item != m_cacheMap.end())
        return item->second;

    if (m_sortedFileSize > 0) {
        const uint8_t firstByte = hash.begin()[WIDTH -1]; // due to our sorting method this is actually the last byte of the hash
        // Limit our search through the items starting with the same byte
        int pos = m_offsets[firstByte] / (WIDTH + sizeof(int));
        int endpos;
        if (firstByte == 255)
            endpos = m_sortedFileSize;
        else
            endpos = m_offsets[firstByte +1];
        endpos = endpos / (WIDTH + sizeof(int));
        endpos -= 1; // last item of section
        while (pos <= endpos) {
            int m = (pos + endpos) / 2;
            uint256 *item = (uint256*)(m_sorted + m * (WIDTH + sizeof(int)));
            const int comp = item->Compare(hash);
            if (comp < 0)
                pos = m + 1;
            else if (comp > 0)
                endpos = m - 1;
            else
                return *reinterpret_cast<int*>(m_sorted + m * (WIDTH + sizeof(int)) + WIDTH);
        }
    }
    for (auto part : m_parts) {
        Q_ASSERT(part->reverseLookup);
        Q_ASSERT(part->sorted);
        int pos = 0;
        int endpos = part->sortedFileSize / (WIDTH + sizeof(int)) - 1;
        while (pos <= endpos) {
            int m = (pos + endpos) / 2;
            uint256 *item = (uint256*)(part->sorted + m * (WIDTH + sizeof(int)));
            const int comp = item->Compare(hash);
            if (comp < 0)
                pos = m + 1;
            else if (comp > 0)
                endpos = m - 1;
            else
                return *reinterpret_cast<int*>(part->sorted + m * (WIDTH + sizeof(int)) + WIDTH);
        }
    }

    return -1;
}

const uint256 &HashList::at(int index) const
{
    Q_ASSERT(index >= 0);
    QMutexLocker lock(&m_mutex);
    if (m_reverseLookup) {
        if (m_reverseLookupFile.size() / qint64(sizeof(int)) < qint64(index))
            throw std::runtime_error("row out of bounds");

        // map index to row
        int row = *reinterpret_cast<int*>(m_reverseLookup + index * sizeof(int));
        const uint256 *dummy = reinterpret_cast<uint256*>(m_sorted + row * (WIDTH + sizeof(int)));
        return *dummy;
    }

    // also check the dirty cache. Do this at end as this is a slow lookup
    for (auto iter = m_cacheMap.begin(); iter != m_cacheMap.end(); ++iter) {
        if (iter->second == index)
            return iter->first;
    }
    return HashStoragePrivate::s_null;
}

void HashList::stabilize()
{
    QMutexLocker lock(&m_mutex);
    auto *part = new HashListPart(QString("%1_%2").arg(m_filebase)
                                  .arg(m_parts.size(), 2, 10, QChar('0')));
    if (!part->sortedFile.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        throw std::runtime_error("Failed to open db file for writing");
    }
    if (!part->reverseLookupFile.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        throw std::runtime_error("Failed to open index file for writing");
    }
    m_parts.append(part);

    PairSorter sorted(m_cacheMap);
    std::sort(sorted.pairs.begin(), sorted.pairs.end(), &sortPairs);
    std::map<int, int> lookupTable;
    for (auto iter = sorted.pairs.begin(); iter != sorted.pairs.end(); ++iter) {
        assert(iter->index >= 0); // no negative numbers, please.
        part->sortedFile.write(reinterpret_cast<const char*>(iter->hash->begin()), WIDTH);
        part->sortedFile.write(reinterpret_cast<const char*>(&iter->index), sizeof(int));
        lookupTable.insert(std::make_pair(iter->index, lookupTable.size()));
    }
    sorted.pairs.clear();
    m_cacheMap.clear();
    part->sortedFile.close();
    part->sortedFileSize = part->sortedFile.size();
    for (auto iter = lookupTable.begin(); iter != lookupTable.end(); ++iter) {
        part->reverseLookupFile.write(reinterpret_cast<const char*>(&iter->second), sizeof(int));
    }
    lookupTable.clear();
    part->reverseLookupFile.close();
    m_log->close();
    m_log->open(QIODevice::WriteOnly | QIODevice::Truncate);
    part->openFiles();
    writeInfoFile();
}

void HashList::fillOffsetsTable()
{
    Q_ASSERT(m_sorted);
    uint32_t data = 0;      // the first byte of the KEY
    uint32_t offset = 0;    // the offset in file.
    while (offset < m_sortedFileSize) {
        // we sort the file by the 'WIDTH'-bytes hash, but the least signficant byte first.
        const uint8_t x = m_sorted[offset + WIDTH - 1];
        if (x > data) {
            do {
                m_offsets[++data] = offset;
            } while (data < x);
        }
        offset += WIDTH + sizeof(int);
    }
    while (data < 255) {
        m_offsets[++data] = offset;
    }
}

void HashList::writeInfoFile() const
{
    QFile info(m_filebase + ".info");
    if (info.open(QIODevice::WriteOnly)) {
        QDataStream out(&info);
        out << m_nextId;
        out << m_parts.length();

        for (int i = 0; i < 256; ++i) {
            out << m_offsets[i];
        }
    }
}

void HashList::finalize()
{
    if (!m_cacheMap.empty())
        stabilize();
    Q_ASSERT(m_cacheMap.empty());
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

    HashCollector collector(m_parts);
    collector.writeHashesToFile(&m_sortedFile);
    collector.writeRevertLookup(&m_reverseLookupFile);

    for (auto &p : m_parts) {
        p->closeFiles();
        p->reverseLookupFile.remove();
        p->sortedFile.remove();
    }
    qDeleteAll(m_parts);
    m_parts.clear();
    m_log->close();
    bool ok = m_log->remove();
    Q_ASSERT(ok); Q_UNUSED(ok)
    delete m_log;
    m_log = nullptr;
    m_sortedFile.close();
    m_sortedFileSize = 0;
    if (m_sortedFile.open(QIODevice::ReadOnly)) {
        m_sortedFileSize = m_sortedFile.size();
        m_sorted = m_sortedFile.map(0, m_sortedFileSize);
        m_sortedFile.close();

        fillOffsetsTable();
    }
    m_reverseLookupFile.close();
    if (m_reverseLookupFile.open(QIODevice::ReadOnly)) {
        m_reverseLookup = m_reverseLookupFile.map(0, m_reverseLookupFile.size());
        m_reverseLookupFile.close();
    }
    writeInfoFile();
}


// -----------------------------------------------------------------

HashStoragePrivate::HashStoragePrivate(const boost::filesystem::path &basedir_)
    : basedir(QString::fromStdWString(basedir_.wstring()))
{
    boost::system::error_code error;
    boost::filesystem::create_directories(basedir_, error);
    if (error && !boost::filesystem::exists(basedir_) && !boost::filesystem::is_directory(basedir_)) {
        logFatal() << "HashStorage can't save. Failed creating the dir:" << basedir_.string();
        return;
    }
    int index = 1;
    const QString fileBase = QString("%1/hashlist-%2").arg(basedir);
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
