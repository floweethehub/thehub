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
#ifndef BUCKETMAP_H
#define BUCKETMAP_H

#include <vector>
#include <atomic>

#include "UnspentOutputDatabase.h"
class BucketMap;

struct OutputRef {
    OutputRef() = default;
    OutputRef(uint64_t cheapHash, uint32_t leafPos, UnspentOutput *output = nullptr)
        : cheapHash(cheapHash), leafPos(leafPos), unspentOutput(output) {
    }
    inline bool operator==(const OutputRef &other) const {
        return cheapHash == other.cheapHash && leafPos == other.leafPos && unspentOutput == other.unspentOutput;
    }
    inline bool operator!=(const OutputRef &other) const { return !operator==(other); }

    uint64_t cheapHash;
    uint32_t leafPos;
    UnspentOutput *unspentOutput;
};

struct Bucket {
    std::vector<OutputRef> unspentOutputs;
    short saveAttempt = 0;

    void fillFromDisk(const Streaming::ConstBuffer &buffer, const int32_t bucketOffsetInFile);
    int32_t saveToDisk(Streaming::BufferPool &pool) const;
};

struct KeyValuePair {
    int k;
    Bucket v;
};

struct BucketMapData {
    std::vector<KeyValuePair> keys;
};

class BucketHolder {
public:
    ~BucketHolder();
    BucketHolder();
    BucketHolder(BucketHolder && other);
    void unlock();

    inline Bucket *operator*() const { return b; }
    inline Bucket *operator->() const { return b; }
    BucketHolder &operator=(BucketHolder && other);

    void insertBucket(int key, const Bucket && bucket);
    void deleteBucket();

protected:
    BucketHolder(BucketMap *p, int index, int key);
private:
    friend class BucketMap;
    BucketMap *p;
    BucketMapData *d;
    Bucket *b;
    int index;
};

class BucketMap
{
public:
    enum { BITS = 12, KEYMASK = (1 << BITS)-1 };
    BucketMap();
    ~BucketMap();

    inline BucketHolder lock(int key) {
        return BucketHolder(this, key % KEYMASK, key);
    }

    class Iterator {
    public:
        Iterator() {}
        ~Iterator();
        Iterator(Iterator && o);
        Iterator(BucketMap *parent, int bucketId, int key);
        inline Bucket &operator*() const {
            return value();
        }
        Bucket &value() const;
        int key() const;
        Iterator &operator++();
        // inline Iterator operator++(int);
        // inline Iterator &operator--();
        // inline Iterator operator--(int);
        inline bool operator==(const Iterator &o) { return o.p == p && o.b == b && o.i == i; }
        inline bool operator!=(const Iterator &o) { return !operator==(o); }
        inline Iterator &operator=(Iterator && o);

    private:
        friend class BucketMap;
        BucketMap *p = nullptr;
        BucketMapData *d = nullptr;
        int b = -1, i = -1;
    };

    inline Iterator begin() { return std::move(++Iterator(this, 0, -1)); }
    inline Iterator end() { return Iterator(this, m.size(), -1); }
    void erase(Iterator &iterator);

private:
    friend class BucketHolder;
    std::vector<std::atomic<BucketMapData*> > m;
};

#endif
