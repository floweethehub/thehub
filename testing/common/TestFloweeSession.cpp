/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2015 The Bitcoin Core developers
 * Copyright (C) 2017-2018 Tom Zander <tomz@freedommail.ch>
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

#include "TestFloweeSession.h"
#include "script/sigcache.h"
#include "MockApplication.h"
#include <Application.h>
#include <utxo/UnspentOutputDatabase.h>
#include <main.h>
#ifdef ENABLE_WALLET
# include <wallet/wallet.h>
CWallet* pwalletMain;
#endif

#include <boost/filesystem.hpp>

CClientUIInterface uiInterface; // Declared but not defined in UiInterface.h

UnspentOutputDatabase *g_utxo = nullptr;

TestFloweeSession::TestFloweeSession(const std::string& chainName) : TestFloweeEnvPlusNet(chainName)
{
    InitSignatureCache();
    if (chainName == CBaseChainParams::REGTEST)
        Application::setUahfChainState(Application::UAHFActive);

#ifdef ENABLE_WALLET
    bitdb.MakeMock();
#endif
    ClearDatadirCache();
    pathTemp = GetTempPath() / strprintf("test_flowee_%lu_%i", (unsigned long)GetTime(), (int)(GetRand(100000)));
    boost::filesystem::create_directories(pathTemp / "regtest/blocks/index");
    boost::filesystem::create_directories(pathTemp / "blocks/index");
    mapArgs["-datadir"] = pathTemp.string();
    Blocks::DB::createTestInstance(1<<20);
    g_utxo = new UnspentOutputDatabase(Application::instance()->ioService(), GetDataDir(true) / "unspent");

    bv.initSingletons();
    bv.appendGenesis();
    bv.waitValidationFinished();
    MockApplication::setValidationEngine(&bv);

#ifdef ENABLE_WALLET
    bool fFirstRun;
    pwalletMain = new CWallet("wallet.dat");
    pwalletMain->LoadWallet(fFirstRun);
    ValidationNotifier().addListener(pwalletMain);
#endif

    RegisterNodeSignals(GetNodeSignals());
}

TestFloweeSession::~TestFloweeSession()
{
    MockApplication::setValidationEngine(nullptr);
    bv.shutdown();
    Blocks::Index::unload();

    UnregisterNodeSignals(GetNodeSignals());
    ValidationNotifier().removeAll();
#ifdef ENABLE_WALLET
    delete pwalletMain;
    pwalletMain = NULL;
#endif
    UnloadBlockIndex();
    delete g_utxo;
#ifdef ENABLE_WALLET
    bitdb.Flush(true);
    bitdb.Reset();
#endif
    boost::filesystem::remove_all(pathTemp);
}
