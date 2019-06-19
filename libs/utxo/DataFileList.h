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
#ifndef DATAFILELIST_H
#define DATAFILELIST_H

class DataFileListPrivate;
class DataFile;
typedef DataFile* ValueType;

class DataFileList
{
public:
    DataFileList();
    DataFileList(const DataFileList &other);
    ~DataFileList();

    DataFileList &operator=(DataFileList & other);

    int size() const;
    void clear();
    DataFile *at(int i) const;
    DataFile *last() const;
    void append(DataFile *datafile);
    ValueType &operator[](int pos);
    bool isEmpty() const;
    void removeLast();

private:
    DataFileListPrivate *d = nullptr;
};

#endif
