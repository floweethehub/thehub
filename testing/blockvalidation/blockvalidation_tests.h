/*
 * This file is part of the flowee project
 * Copyright (C) 2017-2019 Tom Zander <tomz@freedommail.ch>
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
#ifndef TEST_BLOCKVALIDATION_H
#define TEST_BLOCKVALIDATION_H

#include <common/TestFloweeSession.h>

class TestBlockValidation : public TestFloweeSession
{
    Q_OBJECT
public:
    TestBlockValidation();

private slots:
    void reorderblocks();
    void reorderblocks2();
    void detectOrder();
    void detectOrder2();
    void duplicateInput();
    void CTOR();
    void rollback();
    void minimalPush();

    void manualAdjustments();

private:
    FastBlock createHeader(const FastBlock &full) const;
    // this only works if the input is a p2pkh script!
    CTransaction splitCoins(const Tx &inTx, int inIndex, const CKey &from, const CKey &to, int outputCount) const;

};

#endif
