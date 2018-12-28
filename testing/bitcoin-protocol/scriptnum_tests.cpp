/*
 * This file is part of the Flowee project
 * Copyright (C) 2012-2015 The Bitcoin Core developers
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

#include "scriptnum_tests.h"
#include "scriptnum10.h"
#include "script/script.h"
#include "test/test_bitcoin.h"

static const int64_t values[] = \
{ 0, 1, CHAR_MIN, CHAR_MAX, UCHAR_MAX, SHRT_MIN, USHRT_MAX, INT_MIN, INT_MAX, UINT_MAX, LONG_MIN, LONG_MAX };
static const int64_t offsets[] = { 1, 0x79, 0x80, 0x81, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x10000};

static bool verify(const CScriptNum10& bignum, const CScriptNum& scriptnum)
{
    return bignum.getvch() == scriptnum.getvch() && bignum.getint() == scriptnum.getint();
}

static void RunOperators(const int64_t& num1, const int64_t& num2)
{
    const CScriptNum10 bignum1(num1);
    const CScriptNum10 bignum2(num2);
    const CScriptNum scriptnum1(num1);
    const CScriptNum scriptnum2(num2);
    CScriptNum10 bignum3(num1);
    CScriptNum10 bignum4(num1);
    CScriptNum scriptnum3(num1);
    CScriptNum scriptnum4(num1);

    // int64_t overflow is undefined.
    bool invalid = (((num2 > 0) && (num1 > (std::numeric_limits<int64_t>::max() - num2))) ||
                    ((num2 < 0) && (num1 < (std::numeric_limits<int64_t>::min() - num2))));
    if (!invalid) {
        QVERIFY(verify(bignum1 + bignum2, scriptnum1 + scriptnum2));
        QVERIFY(verify(bignum1 + bignum2, scriptnum1 + num2));
        QVERIFY(verify(bignum1 + bignum2, scriptnum2 + num1));
    }
    // CheckSubtract
    // int64_t overflow is undefined.
    invalid = ((num2 > 0 && num1 < std::numeric_limits<int64_t>::min() + num2) ||
               (num2 < 0 && num1 > std::numeric_limits<int64_t>::max() + num2));
    if (!invalid) {
        QVERIFY(verify(bignum1 - bignum2, scriptnum1 - scriptnum2));
        QVERIFY(verify(bignum1 - bignum2, scriptnum1 - num2));
    }

    invalid = ((num1 > 0 && num2 < std::numeric_limits<int64_t>::min() + num1) ||
               (num1 < 0 && num2 > std::numeric_limits<int64_t>::max() + num1));
    if (!invalid) {
        QVERIFY(verify(bignum2 - bignum1, scriptnum2 - scriptnum1));
        QVERIFY(verify(bignum2 - bignum1, scriptnum2 - num1));
    }
    // CheckNegate
    // -INT64_MIN is undefined
    if (num1 != std::numeric_limits<int64_t>::min())
        QVERIFY(verify(-bignum1, -scriptnum1));

    // CheckCompare
    QVERIFY((bignum1 == bignum1) == (scriptnum1 == scriptnum1));
    QVERIFY((bignum1 != bignum1) ==  (scriptnum1 != scriptnum1));
    QVERIFY((bignum1 < bignum1) ==  (scriptnum1 < scriptnum1));
    QVERIFY((bignum1 > bignum1) ==  (scriptnum1 > scriptnum1));
    QVERIFY((bignum1 >= bignum1) ==  (scriptnum1 >= scriptnum1));
    QVERIFY((bignum1 <= bignum1) ==  (scriptnum1 <= scriptnum1));

    QVERIFY((bignum1 == bignum1) == (scriptnum1 == num1));
    QVERIFY((bignum1 != bignum1) ==  (scriptnum1 != num1));
    QVERIFY((bignum1 < bignum1) ==  (scriptnum1 < num1));
    QVERIFY((bignum1 > bignum1) ==  (scriptnum1 > num1));
    QVERIFY((bignum1 >= bignum1) ==  (scriptnum1 >= num1));
    QVERIFY((bignum1 <= bignum1) ==  (scriptnum1 <= num1));

    QVERIFY((bignum1 == bignum2) ==  (scriptnum1 == scriptnum2));
    QVERIFY((bignum1 != bignum2) ==  (scriptnum1 != scriptnum2));
    QVERIFY((bignum1 < bignum2) ==  (scriptnum1 < scriptnum2));
    QVERIFY((bignum1 > bignum2) ==  (scriptnum1 > scriptnum2));
    QVERIFY((bignum1 >= bignum2) ==  (scriptnum1 >= scriptnum2));
    QVERIFY((bignum1 <= bignum2) ==  (scriptnum1 <= scriptnum2));

    QVERIFY((bignum1 == bignum2) ==  (scriptnum1 == num2));
    QVERIFY((bignum1 != bignum2) ==  (scriptnum1 != num2));
    QVERIFY((bignum1 < bignum2) ==  (scriptnum1 < num2));
    QVERIFY((bignum1 > bignum2) ==  (scriptnum1 > num2));
    QVERIFY((bignum1 >= bignum2) ==  (scriptnum1 >= num2));
    QVERIFY((bignum1 <= bignum2) ==  (scriptnum1 <= num2));
}

void TestScriptNum::creation_data()
{
    QTest::addColumn<qint64>("num");


    for(size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        for(size_t j = 0; j < sizeof(offsets) / sizeof(offsets[0]); ++j) {
            qint64 num = values[i];
            QTest::newRow(QString::number(num).toLatin1().constData()) << num;
            num = values[i] + offsets[j];
            QTest::newRow(QString::number(num).toLatin1().constData()) << num;
            num = values[i] - offsets[j];
            QTest::newRow(QString::number(num).toLatin1().constData()) << num;
        }
    }
}

void TestScriptNum::creation()
{
    QFETCH(qint64, num);

    // CheckCreateInt(num);
    CScriptNum10 bignum(num);
    CScriptNum scriptnum(num);
    QVERIFY(verify(bignum, scriptnum));
    QVERIFY(verify(CScriptNum10(bignum.getint()), CScriptNum(scriptnum.getint())));
    QVERIFY(verify(CScriptNum10(scriptnum.getint()), CScriptNum(bignum.getint())));
    QVERIFY(verify(CScriptNum10(CScriptNum10(scriptnum.getint()).getint()), CScriptNum(CScriptNum(bignum.getint()).getint())));

    const bool expectException = (scriptnum.getvch().size() > CScriptNum::nDefaultMaxNumSize);
    try {
        CScriptNum10 bignum(num);
        CScriptNum scriptnum(num);
        QVERIFY(verify(bignum, scriptnum));

        std::vector<unsigned char> vch = bignum.getvch();

        CScriptNum10 bignum2(bignum.getvch(), false);
        vch = scriptnum.getvch();
        CScriptNum scriptnum2(scriptnum.getvch(), false);
        QVERIFY(verify(bignum2, scriptnum2));

        CScriptNum10 bignum3(scriptnum2.getvch(), false);
        CScriptNum scriptnum3(bignum2.getvch(), false);
        QVERIFY(verify(bignum3, scriptnum3));

        if (expectException)
            QFAIL("Expected exception");
    } catch (scriptnum10_error &e) {
        if (!expectException)
            QFAIL("unexpected exception scriptnum10_error");
    } catch (...) {
        QFAIL("unexpected exception");
    }
}

void TestScriptNum::operators()
{
    for(size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        for(size_t j = 0; j < sizeof(offsets) / sizeof(offsets[0]); ++j) {
            RunOperators(values[i], values[i]);
            RunOperators(values[i], -values[i]);
            RunOperators(values[i], values[j]);
            RunOperators(values[i], -values[j]);
            RunOperators(values[i] + values[j], values[j]);
            RunOperators(values[i] + values[j], -values[j]);
            RunOperators(values[i] - values[j], values[j]);
            RunOperators(values[i] - values[j], -values[j]);
            RunOperators(values[i] + values[j], values[i] + values[j]);
            RunOperators(values[i] + values[j], values[i] - values[j]);
            RunOperators(values[i] - values[j], values[i] + values[j]);
            RunOperators(values[i] - values[j], values[i] - values[j]);
        }
    }
}
