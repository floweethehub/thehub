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
#include "BucketMap.h"
#include "UnspentOutputDatabase_p.h"

#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

BucketMap::BucketMap()
    : m(1 << BITS)
{
    for (size_t i = 0; i < m.size(); ++i) {
        m[i] = new BucketMapData();
    }
}

BucketMap::~BucketMap()
{
    for (size_t i = 0; i < m.size(); ++i) {
        delete m[i].load();
    }
}

void BucketMap::erase(BucketMap::Iterator &iterator)
{
    assert(iterator.p == this);
    if (iterator.d) {
        const Bucket *b = &*iterator;
        for (auto iter = iterator.d->keys.begin(); iter != iterator.d->keys.end(); ++iter) {
            if (&iter->v == b) {
                iterator.d->keys.erase(iter);
                --iterator.i;
                ++iterator;
                return;
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////////////////////

BucketHolder::BucketHolder(BucketMap *p, int index, int key)
    : p(p),
      b(nullptr),
      index(index)
{
    while (true) {
        d = p->m[index].exchange(nullptr, std::memory_order_acq_rel);
        if (d)
            break;
        // if 'locked' avoid burning CPU
        struct timespec tim, tim2;
        tim.tv_sec = 0;
        tim.tv_nsec = 500;
        nanosleep(&tim , &tim2);
    }
    for (size_t i = 0; i < d->keys.size(); ++i) {
        if (d->keys.at(i).k == key) {
            b = &d->keys.at(i).v;
            break;
        }
    }
}

BucketHolder::BucketHolder()
    : p(nullptr), d(nullptr), b(nullptr), index(-1)
{
}

BucketHolder::BucketHolder(BucketHolder && other)
    : p(other.p), d(other.d), b(other.b), index(other.index)
{
    other.p = nullptr;
    other.d = nullptr;
}

void BucketHolder::unlock()
{
    if (p && d)
        p->m[index].store(d, std::memory_order_release);
    p = nullptr;
    d = nullptr;
    b = nullptr;
}

BucketHolder &BucketHolder::operator=(BucketHolder && other)
{
    unlock();
    d = other.d;
    p = other.p;
    b = other.b;
    index = other.index;
    other.p = nullptr;
    other.d = nullptr;
    other.b = nullptr;
    return *this;
}

BucketHolder::~BucketHolder()
{
    unlock();
}

void BucketHolder::insertBucket(int key, const Bucket && bucket)
{
    assert(d);
    assert(p);
    d->keys.push_back({key, std::move(bucket)});
    b = &d->keys.at(d->keys.size() - 1).v;
    assert(b);
}

void BucketHolder::deleteBucket()
{
    assert(d);
    assert(p);
    assert(b);
    for (auto iter = d->keys.begin(); iter != d->keys.end(); ++iter) {
        if (&iter->v == b) {
            d->keys.erase(iter);
            b = nullptr;
            return;
        }
    }
}


//////////////////////////////////////////////////////////////////////////////////////////

void Bucket::fillFromDisk(const Streaming::ConstBuffer &buffer, const int32_t bucketOffsetInFile)
{
    assert(bucketOffsetInFile >= 0);
    unspentOutputs.clear();
    Streaming::MessageParser parser(buffer);
    uint64_t cheaphash = 0;
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == UODB::CheapHash) {
            cheaphash = parser.longData();
        }
        else if (parser.tag() == UODB::LeafPosRelToBucket) {
            int offset = parser.intData();
            if (offset > bucketOffsetInFile) {
                logFatal(Log::UTXO) << "Database corruption, offset to bucket messed up"
                                    << offset << bucketOffsetInFile;
                throw std::runtime_error("Database corruption, offset to bucket messed up");
            }
            unspentOutputs.push_back( {cheaphash,
                                       static_cast<std::uint32_t>(bucketOffsetInFile - offset)} );
        }
        else if (parser.tag() == UODB::LeafPosition) {
            unspentOutputs.push_back( {cheaphash, static_cast<std::uint32_t>(parser.intData())} );
        }
        else if (parser.tag() == UODB::LeafPosOn512MB) {
            unspentOutputs.push_back( {cheaphash, static_cast<std::uint32_t>(512 * 1024 * 1024 + parser.intData())} );
        }
        else if (parser.tag() == UODB::LeafPosFromPrevLeaf) {
            if (unspentOutputs.empty())
                throw std::runtime_error("Bucket referred to prev leaf while its the first");
            const int prevLeafPos = static_cast<int>(unspentOutputs.back().leafPos);
            const int newLeafPos = prevLeafPos - parser.intData();
            if (newLeafPos < 0)
                throw std::runtime_error("Invalid leaf pos due to LeafPosFroMPrevLeaf");
            unspentOutputs.push_back( {cheaphash, static_cast<std::uint32_t>(newLeafPos)} );
        }
        else if (parser.tag() == UODB::LeafPosRepeat) {
            if (unspentOutputs.empty())
                throw std::runtime_error("Bucket referred to prev leaf while its the first");
            const int leafPos = static_cast<int>(unspentOutputs.back().leafPos);
            unspentOutputs.push_back( {cheaphash, static_cast<std::uint32_t>(leafPos)} );
        }
        else if (parser.tag() == UODB::Separator) {
#ifdef DEBUG_UTXO
            for (auto uo : unspentOutputs) {
                assert(uo.leafPos < MEMBIT);
            }
#endif
            return;
        }
    }
    throw std::runtime_error("Failed to parse bucket");
}

int32_t Bucket::saveToDisk(Streaming::BufferPool &pool) const
{
    const int32_t offset = pool.offset();

    Streaming::MessageBuilder builder(pool);
    uint64_t prevCH = 0;
    int prevPos = -1;
    for (auto item : unspentOutputs) {
        if (prevCH != item.cheapHash) {
            builder.add(UODB::CheapHash, item.cheapHash);
            prevCH = item.cheapHash;
        }

        /*
         * Figure out which tag to use.
         * To have smaller numbers and we save the one that occupies the lowest amount of bytes
         */
        assert(offset >= 0);
        assert((item.leafPos & MEMBIT) == 0);
        assert(item.leafPos < static_cast<std::uint32_t>(offset));
        const int leafPos = static_cast<int>(item.leafPos);
        UODB::MessageTags tagToUse = UODB::LeafPosition;
        int pos = leafPos;
        int byteCount = Streaming::serialisedIntSize(pos);

        // for values between 256MB and 768MB this moves 1 bit to the tag and avoids
        // the value from going from 4 bytes to 5 bytes.
        // notice that the negative sign is also strored outside the value bytes
        int m512TagSize = 20;
        if (leafPos >= 256 * 1024 * 1024)
            m512TagSize = Streaming::serialisedIntSize(leafPos - 512 * 1024 * 1024);
        if (m512TagSize < byteCount) {
            // store the distance to the 512MB file offset instead of from the start of file.
            tagToUse = UODB::LeafPosOn512MB;
            byteCount = m512TagSize;
            pos = leafPos - 512 * 1024 * 1024;
        }
        const int offsetFromBucketSize = Streaming::serialisedIntSize(offset - leafPos);
        if (offsetFromBucketSize < byteCount) {
            tagToUse = UODB::LeafPosRelToBucket;
            byteCount = offsetFromBucketSize;
            pos = offset - leafPos;
        }
        if (prevPos >= 0) {
            if (Streaming::serialisedIntSize(prevPos - leafPos) < byteCount) {
                tagToUse = UODB::LeafPosFromPrevLeaf;
                pos = prevPos - leafPos;
            }
        }
        if (prevPos == leafPos) // This is often the case when multiple outputs are in a bucket
            builder.add(UODB::LeafPosRepeat, false);
        else
            builder.add(tagToUse, pos);
        prevPos = leafPos;
    }
    builder.add(UODB::Separator, true);
    pool.commit();
    return offset;
}


//////////////////////////////////////////////////////////////////////////////////////////

BucketMap::Iterator::~Iterator()
{
    if (p && d)
        p->m[b].store(d, std::memory_order_release);
    p = nullptr;
    d = nullptr;
}

BucketMap::Iterator::Iterator(BucketMap::Iterator && other)
    : p(other.p), d(other.d), b(other.b), i(other.i)
{
    other.p = nullptr;
    other.d = nullptr;
}

BucketMap::Iterator::Iterator(BucketMap *parent, int bucketId, int key)
    : p(parent), b(bucketId), i(key)
{
}

Bucket &BucketMap::Iterator::value() const
{
    assert(p);
    assert(d);
    assert(d->keys.size() > i);
    return d->keys.at(i).v;
}

int BucketMap::Iterator::key() const
{
    assert(p);
    assert(d);
    assert(d->keys.size() > i);
    return d->keys.at(i).k;
}

BucketMap::Iterator &BucketMap::Iterator::operator++()
{
    while (true) {
        if (d) {
            if (++i >= d->keys.size()) {
                p->m[b].store(d, std::memory_order_release);
                d = nullptr;
                i = -1;
                ++b;
                continue;
            }
            return *this;
        } else if (p->m.size() <= b) {
            break;
        } else {
            while (true) {
                d = p->m[b].exchange(nullptr, std::memory_order_acq_rel);
                if (d)
                    break;
                // if 'locked' avoid burning CPU
                struct timespec tim, tim2;
                tim.tv_sec = 0;
                tim.tv_nsec = 500;
                nanosleep(&tim , &tim2);
            }
            i = -1;
            continue;
        }
    }
    return *this;
}
