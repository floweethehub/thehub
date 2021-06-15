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
#ifndef BUCKETMAP_H
#define BUCKETMAP_H

#include <vector>
#include <atomic>

#include "UnspentOutputDatabase.h"
class BucketMap;

/**
 * An output-ref is a pointer (or reference) to an unspent output.
 * The unspent output is represented by a "Leaf" which is stored on-disk at a certain offset in file
 * that we store in leafPos, which has its in-memory representation via class UnspentOutput.
 *
 * A bucket is basically just a (sorted) list of OutputRefs.
 */
struct OutputRef {
    OutputRef() = default;
    OutputRef(uint64_t cheapHash, uint32_t leafPos, UnspentOutput *output = nullptr)
        : cheapHash(cheapHash), leafPos(leafPos), unspentOutput(output) {
    }
    inline bool operator==(const OutputRef &other) const {
        return cheapHash == other.cheapHash && leafPos == other.leafPos && unspentOutput == other.unspentOutput;
    }
    inline bool operator!=(const OutputRef &other) const { return !operator==(other); }

    /// An output has as key 'txid+output-index'. The cheapHash is the first 8 bytes of the txid.
    uint64_t cheapHash;
    uint32_t leafPos;
    UnspentOutput *unspentOutput;
};

/**
 * The actual data stored in the map, a decoded Bucket.
 */
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

/**
 * A BucketHolder allows one to reference a 'locked' bucket.
 * The BucketMap only allows buckets stored in there to be accessed exclusively which
 * requires a call to BucketMap.lock(). This class is a simple wrapper to make this
 * transparant and safe. Calling unlock() or causing the destructor to be called will
 * restore the bucket ownership to the BucketMap.
 *
 * Be certain to no longer access the Bucket pointer after unlock() has been callled.
 */
class BucketHolder {
public:
    BucketHolder();
    /// Destructor calls unlock()
    ~BucketHolder();
    BucketHolder(BucketHolder && other);
    /// should this represent a locked bucket, unlocking it will forget the bucket and move ownership back to the map.
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

/**
 * The BucketMap has a mapping from 'index' to a BucketMapData, which can be lock-free taken ownership of.
 * This class implements a lock-free design to have a massive amount of items which can be accessed or modified
 * in a thread-safe manner.
 *
 * This map will remind people of the standard implementation of a hashmap, just with hardcoded size.
 * The initial list has a size set by BITS and each item is a list of its own (BucketMapData) to allow
 * storing of items (KeyValuePair) with the unique key of size 'int'.
 *
 * Each item in our vector is an atomic pointer and to ensure thread-safety anyone reading or writing any of the data items
 * needs to first claim ownership of it. Internally this map works together with the BucketHolder and ownership is claimed
 * by resetting the pointer to become a nullpointer and transferring the ownership to the BucketMap. When that one is unlocked
 * the nullpointer will be reset to the real pointer to the BucketMapData again.
 *
 * If you are not familiar with atomics and lock-free, just imagine one mutex for each of the BucketMapData items.
 */
class BucketMap
{
public:
    enum { BITS = 12, KEYMASK = (1 << BITS)-1 };
    BucketMap();
    ~BucketMap();

    inline BucketHolder lock(int key) {
        return BucketHolder(this, key % KEYMASK, key);
    }

    /**
     * The iterator to read each value in the map.
     * This automatically takes care of locking and will wait until an item becomes available while iterating.
     */
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
