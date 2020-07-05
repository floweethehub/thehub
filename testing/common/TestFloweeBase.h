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
#ifndef FLOWEE_TEST_H
#define FLOWEE_TEST_H

#include <QObject>
#include <QtTest/QtTest>

class TestFloweeBase : public QObject {
    Q_OBJECT
public:
    TestFloweeBase();

protected:
    /**
     * Similar to BOOST_CHECK_EQUAL_COLLECTIONS, allow comparing of collections.
     */
    template<class TARGET, class EXPECTED>
    void compare(TARGET target, EXPECTED expected) {
        auto iter = target.begin();
        auto iter2 = expected.begin();
        while (iter != target.end()) {
            QVERIFY(iter2 != expected.end());
            QCOMPARE(*iter, *iter2);
            ++iter;
            ++iter2;
        }
        QVERIFY(iter2 == expected.end());
    }

private:
    const char *currentTestName();
    char m_currentTestname[40];
    const char *m_prevTest = nullptr;
};

#endif
