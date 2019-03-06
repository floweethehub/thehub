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
#include "DataFileList.h"
#include "UnspentOutputDatabase_p.h"

#include <deque>
#include <atomic>
#include <utility>

class DataFileListPrivate
{
public:
    DataFileListPrivate() : ref(1) {}

    DataFileListPrivate(DataFileListPrivate *other) : ref(1), list(other->list) {}

    mutable std::atomic_int ref;
    std::deque<DataFile*> list;
};

DataFileList::DataFileList()
    : d(new DataFileListPrivate())
{
}

DataFileList::DataFileList(const DataFileList &other)
    : d(other.d)
{
    d->ref.fetch_add(1);
}

DataFileList::~DataFileList()
{
    if (d->ref.fetch_sub(1) == 1)
        delete d;
}

DataFileList &DataFileList::operator=(DataFileList &other)
{
    other.d->ref.fetch_add(1);
    if (d->ref.fetch_sub(1) == 1)
        delete d;
    d = other.d;
    return *this;
}

int DataFileList::size() const
{
    return static_cast<int>(d->list.size());
}

void DataFileList::clear()
{
    d->list.clear();
}

DataFile *DataFileList::at(int i) const
{
    return d->list.at(i);
}

DataFile *DataFileList::last() const
{
    return d->list.back();
}

void DataFileList::append(DataFile *datafile)
{
    auto priv = new DataFileListPrivate(d);
    priv->list.push_back(datafile);
    std::swap(priv, d);
    if (priv->ref.fetch_sub(1) == 1)
        delete priv;
}

ValueType &DataFileList::operator[](int pos)
{
    auto priv = new DataFileListPrivate(d);
    std::swap(priv, d);
    if (priv->ref.fetch_sub(1) == 1)
        delete priv;
    return d->list[pos];
}

bool DataFileList::isEmpty() const
{
    return d->list.empty();
}

