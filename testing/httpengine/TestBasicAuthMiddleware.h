/*
 * This file is part of the Flowee project
 * Copyright (c) 2017 Nathan Osman
 * Copyright (C) 2019 Tom Zander <tom@flowee.org>
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

#include <QTest>

#include <httpengine/basicauthmiddleware.h>

class TestBasicAuthMiddleware : public QObject
{
    Q_OBJECT
public:
    TestBasicAuthMiddleware() : auth("Test") {}

private slots:
    void initTestCase();

    void testProcess_data();
    void testProcess();

private:
    HttpEngine::BasicAuthMiddleware auth;
};
