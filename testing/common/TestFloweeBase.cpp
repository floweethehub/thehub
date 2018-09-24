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

#include "TestFloweeBase.h"
#include <Logger.h>


TestFloweeBase::TestFloweeBase()
{
    memset(m_currentTestname, 0, sizeof(m_currentTestname));
    Log::Manager::instance()->loadDefaultTestSetup(std::bind(&TestFloweeBase::currentTestName, this));
}

const char *TestFloweeBase::currentTestName()
{
    if (m_prevTest == QTest::currentTestFunction())
        return m_currentTestname;
    m_prevTest = QTest::currentTestFunction();

    const char * classname = metaObject()->className();
    size_t len1 = strlen(classname);
    const char * testName = QTest::currentTestFunction();
    if (!testName)
        return nullptr;
    const size_t len2 = strlen(testName);
    const size_t total = len1 + len2 + 2;
    size_t offset = total >= sizeof(m_currentTestname) ? total - sizeof(m_currentTestname) : 0;
    if (offset > len1)
        len1 = 0;
    else
        len1 -= offset;
    memset(m_currentTestname, 0, sizeof(m_currentTestname));
    strncpy(m_currentTestname, classname + offset, len1);
    m_currentTestname[len1] = '/';
    strncpy(m_currentTestname + len1 + 1, testName,
            std::min(len2, sizeof(m_currentTestname) - len1));
    return  m_currentTestname;
}
