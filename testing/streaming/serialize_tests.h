/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tom@flowee.org>
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
#ifndef SERIALIZE_TESTS_H
#define SERIALIZE_TESTS_H

#include <common/TestFloweeBase.h>

class Test_Serialize : public TestFloweeBase
{
    Q_OBJECT
private slots:
    void sizes();
    void floats_conversion();
    void doubles_conversion();
    void floats();
    void doubles();
    void varints();
    void compactsize();
    void noncanonical();
    void insert_delete();
};

#endif
