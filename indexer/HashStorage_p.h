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
#ifndef HASHSTORAGE_p_H
#define HASHSTORAGE_p_H

/*
 * WARNING USAGE OF THIS HEADER IS RESTRICTED.
 * This Header file is part of the private API and is meant to be used solely by the HashStorage component.
 *
 * Usage of this API will likely mean your code will break in interesting ways in the future,
 * or even stop to compile.
 *
 * YOU HAVE BEEN WARNED!!
 */

#include <qvector.h>
#include <uint256.h>

#include <boost/filesystem.hpp>
#include <qfile.h>
#include <qmap.h>
#include <qmutex.h>
#include <unordered_map>

inline uint32_t qHash(const uint256 &key, uint32_t seed) {
    return *reinterpret_cast<const uint32_t*>(key.begin() + (seed % 5));
}

class HashListPart
{
public:
    HashListPart(const QString &partBase);
    ~HashListPart();
    void openFiles();
    void closeFiles();
    int find(const uint256 &hash) const;
    const uint256 &at(int index) const;

    uchar *sorted = nullptr;
    QFile sortedFile;
    qint64 sortedFileSize = 0;

    uchar *reverseLookup = nullptr;
    QFile reverseLookupFile;
};

class HashList {
public:
    HashList(const QString &dbBase);
    ~HashList();
    static HashList *createEmpty(const QString &dbBase, int index);

    int append(const uint256 &hash);
    int lookup(const uint256 &hash) const;
    const uint256 &at(int index) const;
    void writeInfoFile() const;

    // write the m_cache to disk (sorted) and start a new one.
    void stabilize();

    // copy all parts into one file and switch to (single) file lookups only
    void finalize();

    const QString m_filebase;
    QList<HashListPart*> m_parts;

    // for the memmapped, sorted section.
    uchar *m_sorted = nullptr;
    QFile m_sortedFile;
    qint64 m_sortedFileSize = 0;
    uchar *m_reverseLookup = nullptr;
    QFile m_reverseLookupFile;

    // the unsorted part
    QFile *m_log = nullptr;
    std::unordered_map<uint256, int, HashShortener, HashComparison> m_cacheMap;

    int m_nextId = 0;
    mutable QMutex m_mutex;
};

class HashStoragePrivate
{
public:
    HashStoragePrivate(const boost::filesystem::path &basedir);
    ~HashStoragePrivate();

    QList<HashList*> dbs;
    const QString basedir;

    static uint256 s_null;
};

#endif
