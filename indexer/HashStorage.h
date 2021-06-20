/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tom@flowee.org>
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
#ifndef HASHSTORAGE_H
#define HASHSTORAGE_H

#include <uint256.h>
#include <boost/filesystem.hpp>

struct HashIndexPoint
{
    HashIndexPoint() = default;
    HashIndexPoint(int db, int row) : db(db),row(row) {}
    HashIndexPoint(const HashIndexPoint &other) = default;
    int db = -1;
    int row = -1;
};

inline bool operator==(const HashIndexPoint &a, const HashIndexPoint &b)
{
    return a.db == b.db && a.row == b.row;
}

class HashStoragePrivate;

/// A fast lookup storage of hashes.
class HashStorage
{
public:
    HashStorage(const boost::filesystem::path &basedir);
    ~HashStorage();

    /// return the amount of databases
    /// @see HashIndexPoint::db
    int databaseCount() const;

    /// Append the hash and return its unique point.
    /// Please note behaviour is not defined if the same hash is appended twice.
    HashIndexPoint append(const uint256 &hash);
    /// Find in the collection the hash by point. This is an expensive operation!
    const uint256 &find(HashIndexPoint point) const;
    /// return the point given to the hash.
    HashIndexPoint lookup(const uint256 &hash) const;

    /// Flush all caches and make lookup on-disk only
    void finalize();

private:
    HashStoragePrivate *d;
};

#endif
