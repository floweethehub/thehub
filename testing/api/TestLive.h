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
#ifndef TESTAPI_H
#define TESTAPI_H

#include "BlackBoxTest.h"

class TestApiLive : public BlackBoxTest
{
    Q_OBJECT
private slots:
    void testBasic();
    void testSendTx();
    void testUtxo();
    void testGetMempoolInfo();
    void testGetTransaction();

private:
    // Mine 100 blocks onto a new address and return address
    Streaming::ConstBuffer generate100(int nodeId = 0);
};

#endif
