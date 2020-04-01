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
#include "bip32_tests.h"
#include "bloom_tests.h"
#include "uint256_tests.h"
#include "checkdatasig_tests.h"
#include "pow_tests.h"
#include "pmt_tests.h"
#include "script_P2SH_tests.h"
#include "script_tests.h"
#include "scriptnum_tests.h"
#include "transaction_tests.h"
#include "DoubleSpendProofTest.h"
#include "multisig_tests.h"

int main(int, char **)
{
    int rc = 0;
    {
        TestBip32 test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestUint256 test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        CheckDataSig test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        POWTests test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestPaymentToScriptHash test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestScript test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestScriptNum test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TransactionTests test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        DoubleSpendProofTest test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        MultiSigTests test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestBloom test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        PMTTests test;
        rc = QTest::qExec(&test);
    }
    return rc;
}
