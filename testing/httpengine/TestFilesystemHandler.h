/*
 * This file is part of the Flowee project
 * Copyright (c) 2017 Nathan Osman
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

#pragma once

#include <QObject>
#include <QTemporaryDir>

class TestFilesystemHandler : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase();

    void testRangeRequests_data();
    void testRangeRequests();

    void testRequests_data();
    void testRequests();

private:

    bool createFile(const QString &path);
    bool createDirectory(const QString &path);

    QTemporaryDir dir;
};
