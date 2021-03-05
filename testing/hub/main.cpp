/*
 * This file is part of the Flowee project
 * Copyright (C) 2018-2021 Tom Zander <tom@flowee.org>
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
#include "blocksdb_tests.h"
#include "MetaBlock_tests.h"

int main(int, char**)
{
    int rc = 0;
    {
        TestBlocksDB test;
        rc = QTest::qExec(&test);
    }
    if (!rc) {
        TestMetaBlock test;
        rc = QTest::qExec(&test);
    }

    return rc;
}
