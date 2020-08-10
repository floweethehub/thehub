/*
 * This file is part of the Flowee project
 * Copyright (C) 2018-2020 Tom Zander <tomz@freedommail.ch>
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
#ifndef POW_TESTS_H
#define POW_TESTS_H

#include <common/TestFloweeBase.h>

class CBlockIndex;

class POWTests : public TestFloweeBase
{
    Q_OBJECT
private:
    double TargetFromBits(const uint32_t nBits) const;

    double GetASERTApproximationError(const CBlockIndex *pindexPrev, const uint32_t finalBits,
                                      const CBlockIndex *pindexReferenceBlock) const;
private slots:
    void get_next_work();
    void get_next_work_pow_limit();
    void get_next_work_lower_limit_actual();
    void get_next_work_upper_limit_actual();
    void GetBlockProofEquivalentTime_test();
    void retargeting_test();
    void cash_difficulty_test();

    // asert (from 2020)
    void asert_difficulty_test();
    void calculate_asert_test();
    void asert_activation_anchor_test();
};

#endif
