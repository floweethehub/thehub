/*
 * This file is part of the Flowee project
 * Copyright (c) 2015 The Bitcoin Core developers
 * Copyright (C) 2017-2019 Tom Zander <tom@flowee.org>
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

#ifndef FLOWEE_TEST_SESSION_H
#define FLOWEE_TEST_SESSION_H

#include "TestFloweeEnvPlusNet.h"
#include "MockBlockValidation.h"

/** Testing setup that configures a complete environment.
 * Included are data directory, script check threads
 * and wallet (if enabled) setup.
 */
class TestFloweeSession : public TestFloweeEnvPlusNet
{
    Q_OBJECT
public:
    std::unique_ptr<MockBlockValidation> bv;
    boost::filesystem::path pathTemp;

    enum BlocksDb {
        BlocksDbInMemory,
        BlocksDbOnDisk
    };

    TestFloweeSession(const std::string& chainName = CBaseChainParams::REGTEST);
    ~TestFloweeSession();

protected slots:
    /// called before each test
    void init();
    /// called after each test
    void cleanup();
};

class MainnetTestFloweeSession : TestFloweeSession
{
    Q_OBJECT
public:
    MainnetTestFloweeSession() : TestFloweeSession(CBaseChainParams::MAIN) {}
};

#endif
