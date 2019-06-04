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
#include "TestBasicAuthMiddleware.h"
#include "TestFilesystemHandler.h"
#include "TestParser.h"
#include "TestHandler.h"
#include "TestLocalFile.h"
#include "TestLocalAuthMiddleware.h"
#include "TestMiddleware.h"
#include "TestQIODeviceCopier.h"
#include "TestRange.h"
#include "TestServer.h"
#include "TestSocket.h"

int main(int x, char **y)
{
    QCoreApplication app(x,y);
    int rc = 0;
    {
        TestBasicAuthMiddleware test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestFilesystemHandler test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestParser test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestHandler test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestLocalFile test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestLocalAuthMiddleware test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestMiddleware test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestQIODeviceCopier test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestRange test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestServer test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestSocket test;
        rc = QTest::qExec(&test);
    }
    return rc;
}
