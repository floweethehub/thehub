/*
 * This file is part of the Flowee project
 * Copyright (c) 2017 Nathan Osman
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

#include "TestLocalFile.h"
#include <QCoreApplication>
#include <QDir>
#include <QObject>
#include <QTest>

#include <httpengine/localfile.h>

const QString ApplicationName = "HttpEngine";


void TestLocalFile::initTestCase()
{
    QCoreApplication::setApplicationName(ApplicationName);
}

void TestLocalFile::testOpen()
{
    HttpEngine::LocalFile file;
    QVERIFY(file.open());
    QVERIFY(file.remove());
}

