/*
 * This file is part of the Flowee project
 * Copyright (C) 2020 Tom Zander <tomz@freedommail.ch>
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
#ifndef ARITHUINT256_TESTS_H
#define ARITHUINT256_TESTS_H

#include <common/TestFloweeBase.h>

class TestArith256 : public TestFloweeBase
{
    Q_OBJECT
private slots:
    void basics(); // constructors, equality, inequality
    void shifts();  // "<<"  ">>"  "<<="  ">>="
    void unaryOperators(); // !    ~    -
    void bitwiseOperators();
    void comparison(); // <= >= < >
    void plusMinus();
    void multiply();
    void divide();
    void methods(); // GetHex SetHex size() GetLow64 GetSerializeSize, Serialize, Unserialize
    void bignum_SetCompact();
    void getmaxcoverage(); // some more tests just to get 100% coverage
};

#endif