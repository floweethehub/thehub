/*
 * This file is part of the Flowee project
 * Copyright (c) 2015 The Bitcoin Core developers
 * Copyright (C) 2017-2021 Tom Zander <tom@flowee.org>
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

#ifndef MEMPOOLHELPER_H
#define MEMPOOLHELPER_H

#include <txmempool.h>

class CTxMemPoolEntry;
class CTxMemPool;

struct TestMemPoolEntryHelper
{
    // Default values
    int64_t nFee;
    int64_t nTime;
    double dPriority;
    unsigned int nHeight;
    bool hadNoDependencies;
    bool spendsCoinbase;
    LockPoints lp;

    TestMemPoolEntryHelper() :
        nFee(0), nTime(0), dPriority(0.0), nHeight(1),
        hadNoDependencies(false), spendsCoinbase(false) { }

    CTxMemPoolEntry FromTx(CMutableTransaction &tx, CTxMemPool *pool = nullptr);

    // Change the default value
    TestMemPoolEntryHelper &Fee(int64_t _fee) { nFee = _fee; return *this; }
    TestMemPoolEntryHelper &Time(int64_t _time) { nTime = _time; return *this; }
    TestMemPoolEntryHelper &Priority(double _priority) { dPriority = _priority; return *this; }
    TestMemPoolEntryHelper &Height(unsigned int _height) { nHeight = _height; return *this; }
    TestMemPoolEntryHelper &HadNoDependencies(bool _hnd) { hadNoDependencies = _hnd; return *this; }
    TestMemPoolEntryHelper &SpendsCoinbase(bool _flag) { spendsCoinbase = _flag; return *this; }
};

#endif
