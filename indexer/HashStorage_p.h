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

#include <qvector.h>
#include <uint256.h>

#include <boost/unordered_map.hpp>
#include <boost/filesystem.hpp>
#include <qfile.h>
#include <qmap.h>
#include <qmutex.h>

class HashList {
public:
    HashList(const QString &dbBase);
    ~HashList();
    static HashList *createEmpty(const QString &dbBase);

    int append(const uint160 &hash);
    int find(const uint160 &hash) const;
    const uint160 &at(int row) const;
    void finalize();

    // for the memmapped, sorted section.
    uchar *m_sorted = nullptr;
    QFile *m_sortedFile = nullptr;
    QVector<int> m_jumptables;

    // the unsorted part
    QFile *m_log = nullptr;
    QMap<int, uint160> m_cacheMap;

    // a id to row mapping to be (re)created at sort
    QMap<int, int> m_resortMap;
    QMap<int, int> m_resortMapReversed;
    int m_nextId = 0;
    mutable QMutex m_mutex;
    QString m_filebase;
};

class HashStoragePrivate
{
public:
    HashStoragePrivate(const boost::filesystem::path &basedir);
    ~HashStoragePrivate();

    QList<HashList*> dbs;
    const QString basedir;

    static uint160 s_null;
};

#endif
