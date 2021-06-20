/*
 * This file is part of the Flowee project
 * Copyright (c) 2015 The Bitcoin Core developers
 * Copyright (C) 2017 Tom Zander <tom@flowee.org>
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

#ifndef BASICTESTINGSETUP_H
#define BASICTESTINGSETUP_H

#include "TestFloweeBase.h"

#include <chainparamsbase.h>
#include <primitives/pubkey.h>

class TestFloweeEnvPlusNet : public TestFloweeBase
{
    Q_OBJECT
public:
    ECCVerifyHandle globalVerifyHandle;

    TestFloweeEnvPlusNet(const std::string& chainName = CBaseChainParams::MAIN);
    ~TestFloweeEnvPlusNet();
};

#endif
