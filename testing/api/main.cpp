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
#include "TestLive.h"

#include <QTest>

int main(int x, char **y)
{
    if (x > 1) {
        QFileInfo file(QString::fromLatin1(y[1]));
        if (file.exists())
            BlackBoxTest::setHubExecutable(file.absoluteFilePath());
    }
    int rc = 0;
    {
        TestApiLive test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
    }
    return rc;
}
