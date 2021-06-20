/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2015 The Bitcoin Core developers
 * Copyright (C) 2017-2018 Tom Zander <tom@flowee.org>
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

#include "TestFloweeEnvPlusNet.h"
#include "MockApplication.h"
#include <chainparams.h>
#include <primitives/key.h>
#include <serverutil.h>

extern void noui_connect();

TestFloweeEnvPlusNet::TestFloweeEnvPlusNet(const std::string &chainName)
{
    ECC_Start();
    SetupEnvironment();
    SetupNetworking();
    mapArgs["-checkblockindex"] = "1";
    SelectParams(chainName);
    noui_connect();
    MockApplication::doStartThreads();
    MockApplication::doInit();
}

TestFloweeEnvPlusNet::~TestFloweeEnvPlusNet()
{
    ECC_Stop();
    Application::quit(0);
}

void Shutdown(void* parg)
{
    exit(0);
}

void StartShutdown()
{
    exit(0);
}

bool ShutdownRequested()
{
    return false;
}
