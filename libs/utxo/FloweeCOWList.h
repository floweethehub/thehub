/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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
#ifndef COWLIST_H
#define COWLIST_H

#include <atomic>
#include <assert.h>
#include <cstdlib>
#include <algorithm>
#include <string.h>

struct COWListData
{
    COWListData(int newCount, int itemSize);
    COWListData(); // creates an uninitialized data object

    std::atomic<int> ref;
    int alloc, size;
    void *array;
};

template <typename T>
/**
 * This is a copy-on-write list which allows for lock-free operations.
 *
 * This list is essentially backed by an array, making appends and lookups
 * have about the same cost as a plain array.
 * Appending will copy the list contents should in case there are others that have
 * a reference to the list.
 */
class COWList
{
public:
    COWList()
    {
    }

    COWList(const COWList &other) // copy constructor
    {
        if (other.d) {
            ++other.d->ref;
            d = other.d;
        }
    }

    COWList &operator=(const COWList &other) {
        ++other.d->ref;
        if (!--d->ref)
            delete d;
        d = other.d;
    }

    ~COWList() {
        if (d && !--d->ref)
            delete d;
    }
    // decrease the refcounter, and delete if zero
    enum { _DataSize = sizeof(T) };

    void append(const T &t) {
        create();
        T *x = reinterpret_cast<T*>(detach_and_grow(1));
        *x = t;
        d->size++;
    }
    inline void push_back(const T &t) {
        append(t);
    }
    int size() const {
        if (!d)
            return 0;
        return d->size;
    }
    T at(int i) const {
        assert(i >= 0);
        assert(d);
        assert(i < d->size);
        return *reinterpret_cast<T*>(reinterpret_cast<char*>(d->array) + i * _DataSize);
    }

    bool empty() const {
        return !d || d->size == 0;
    }

    T last() const {
        assert(d);
        assert(d->size > 0);
        return *reinterpret_cast<T*>(reinterpret_cast<char*>(d->array) + (d->size - 1) * _DataSize);
    }

    void clear() {
        if (d) {
            if (!--d->ref)
                delete d;
            d = nullptr;
        }
    }

    T &operator[](int i) {
        assert(i >= 0);
        assert(d);
        assert(i < d->size);
        detach_and_grow(0);
        return *reinterpret_cast<T*>(reinterpret_cast<char*>(d->array) + i * _DataSize);
    }
    const T &operator[](int i) const {
        assert(i >= 0);
        assert(d);
        assert(i < d->size);
        return *reinterpret_cast<T*>(reinterpret_cast<char*>(d->array) + i * _DataSize);

    }


private:
    char *detach_and_grow(int count) {
        assert(d);
        if (d->ref.load() != 1 || d->alloc - d->size < count) {
            COWListData *data = new COWListData();
            data->alloc = std::max(d->alloc, d->size + count);
            data->size = d->size;
            data->array = ::malloc(data->alloc * _DataSize);
            ::memcpy(data->array, d->array, d->size * _DataSize);

            COWListData *old = d;
            d = data;
            if (!--old->ref)
                delete old;
        }
        return reinterpret_cast<char*>(d->array) + d->size * _DataSize;
    }
    inline void create() {
        if (!d) d = new COWListData(10, _DataSize);
    }
    COWListData *d = nullptr;
};

#endif
