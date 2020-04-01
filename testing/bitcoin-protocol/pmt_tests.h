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
#ifndef PARTIAL_MERKLE_TREE_TESTS_H
#define PARTIAL_MERKLE_TREE_TESTS_H

#include <common/TestFloweeEnvPlusNet.h>

class PMTTests : public TestFloweeEnvPlusNet
{
    Q_OBJECT
private slots:
    void basics();
    void malleability();
};

#endif
