/*
 * This file is part of the Flowee project
 * Copyright (c) 2009-2010 Satoshi Nakamoto
 * Copyright (c) 2009-2015 The Bitcoin Core developers
 * Copyright (C) 2018-2019 Tom Zander <tomz@freedommail.ch>
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

#include "chainparams.h"
#include "clientversion.h"
#include "rpcserver.h"
#include <init.h>
#include <server/main.h>
#include "noui.h"
#include "scheduler.h"
#include <serverutil.h>
#include "httpserver.h"
#include "httprpc.h"
#include "rpcserver.h"
#include "Application.h"
#include "APIServer.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include <AddressMonitorService.h>
#include <TransactionMonitorService.h>
#include <BlockNotificationService.h>
#include <cstdio>

static bool fDaemon;

void WaitForShutdown(boost::thread_group* threadGroup)
{
    bool fShutdown = ShutdownRequested();
    // Tell the main threads to shutdown.
    while (!fShutdown)
    {
        MilliSleep(200);
        fShutdown = ShutdownRequested();
    }
    if (threadGroup)
    {
        Interrupt(*threadGroup);
        threadGroup->join_all();
    }
}

static bool SelectChain()
{
    try {
        SelectParams(ChainNameFromCommandLine());
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return false;
    }
}


//////////////////////////////////////////////////////////////////////////////
//
// Start
//
bool AppInit(int argc, char* argv[])
{
    boost::thread_group threadGroup;
    CScheduler scheduler;

    bool fRet = false;

    //
    // Parameters
    //
    // If Qt is used, parameters/flowee.conf are parsed in qt/bitcoin.cpp's main()
    Settings::Hub allowedArgs;
    try {
        ParseParameters(argc, argv, allowedArgs);
    } catch (const std::exception& e) {
        fprintf(stderr, "Error parsing program options: %s\n", e.what());
        return false;
    }

    // Process help and version before taking care about datadir
    if (mapArgs.count("-?") || mapArgs.count("-h") ||  mapArgs.count("-help") || mapArgs.count("-version"))
    {
        std::string strUsage = _("Flowee the Hub") + " " + _("version") + " " + FormatFullVersion() + "\n";

        if (!mapArgs.count("-version")) {
            strUsage += "\n" + _("Usage:") + "\n" +
                  "  hub [options]                          " + _("Start Flowee the Hub") + "\n";

            strUsage += "\n" + allowedArgs.helpMessage();
        }

        fprintf(stdout, "%s", strUsage.c_str());
        return false;
    }

    std::unique_ptr<Api::Server> apiServer;
    std::unique_ptr<TransactionMonitorService> transactionMonitorService;
    std::unique_ptr<AddressMonitorService> addressMonitorService;
    std::unique_ptr<BlockNotificationService> blockNotificationService;
    try
    {
        for (int i = 1; i < argc; i++) {
            if (!IsSwitchChar(argv[i][0])) {
                fprintf(stderr, "Error: unexpected argument found. Options go in the form of -name=value\n");
                exit(1);
                break;
            }
        }
        std::string dd = GetArg("-datadir", "");
        if (!dd.empty()) {
            auto path = boost::filesystem::system_complete(dd);
            if (!boost::filesystem::is_directory(path)) {
                fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", dd.c_str());
                return false;
            }
        }
        boost::filesystem::path pathConfigFile = GetArg("-conf", "");
        const bool confPathSet = pathConfigFile.is_complete();

        if (!confPathSet) // first select chain, so we read the right conf file.
            if (!SelectChain()) return false;

        try {
            ReadConfigFile(mapArgs, mapMultiArgs);
        } catch (const std::exception& e) {
            fprintf(stderr,"Error reading configuration file: %s\n", e.what());
            return false;
        }
        if (confPathSet) // after raeding the user-indicated conf file, select chain (including conf file opts)
            if (!SelectChain()) return false;

#ifndef WIN32
        fDaemon = GetBoolArg("-daemon", false);
        if (fDaemon)
        {
            fprintf(stdout, "Flowee Hub server starting\n");

            // Daemonize
            pid_t pid = fork();
            if (pid < 0)
            {
                fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
                return false;
            }
            if (pid > 0) // Parent process, pid is child process id
            {
                return true;
            }
            // Child process falls through to rest of initialization

            pid_t sid = setsid();
            if (sid < 0)
                fprintf(stderr, "Error: setsid() returned %d errno %d\n", sid, errno);
        }
#endif
        SoftSetBoolArg("-server", true);

        // Set this early so that parameter interactions go to console
        InitLogging();
        InitParameterInteraction();
        fRet = AppInit2(threadGroup, scheduler);

        if (fRet) {
            if (GetBoolArg("-api", true)) {
                apiServer.reset(new Api::Server(Application::instance()->ioService()));
                addressMonitorService.reset(new AddressMonitorService());
                transactionMonitorService.reset(new TransactionMonitorService());
                blockNotificationService.reset(new BlockNotificationService());
                extern CTxMemPool mempool;
                addressMonitorService->setMempool(&mempool);
                transactionMonitorService->setMempool(&mempool);
                apiServer->addService(addressMonitorService.get());
                apiServer->addService(transactionMonitorService.get());
                apiServer->addService(blockNotificationService.get());
            }
        }
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...) {
        PrintExceptionContinue(nullptr, "AppInit()");
    }

    if (!fRet)
    {
        Interrupt(threadGroup);
        // threadGroup.join_all(); was left out intentionally here, because we didn't re-test all of
        // the startup-failure cases to make sure they don't result in a hang due to some
        // thread-blocking-waiting-for-another-thread-during-startup case
    } else {
        WaitForShutdown(&threadGroup);
    }
    addressMonitorService.reset();
    transactionMonitorService.reset();
    apiServer.reset();
    Shutdown();

    return fRet;
}

int main(int argc, char* argv[])
{
    SetupEnvironment();

    // Connect hub signal handlers
    noui_connect();

    return (AppInit(argc, argv) ? 0 : 1);
}
