/*
 * This file is part of the Flowee project
 * Copyright (c) 2009-2010 Satoshi Nakamoto
 * Copyright (c) 2009-2015 The Bitcoin Core developers
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

#if defined(HAVE_CONFIG_H)
#include "config/flowee-config.h"
#endif

#include "init.h"
#include <SettingsDefaults.h>

#include "Application.h"
#include "addrman.h"
#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "compat/sanity.h"
#include "consensus/validation.h"
#include "httpserver.h"
#include "httprpc.h"
#include <primitives/key.h>
#include "main.h"
#include "miner.h"
#include "net.h"
#include "policy/policy.h"
#include "rpcserver.h"
#include "script/standard.h"
#include "script/sigcache.h"
#include "scheduler.h"
#include "BlocksDB.h"
#include "txmempool.h"
#include "torcontrol.h"
#include "UiInterface.h"
#include <util.h>
#include "serverutil.h"
#include <utilmoneystr.h>
#include <utilstrencodings.h>
#include "txorphancache.h"
#include "validationinterface.h"
#ifdef ENABLE_WALLET
#include "wallet/db.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#endif
#include <validation/Engine.h>
#include <cstdint>
#include <cstdio>

#ifndef WIN32
#include <csignal>
#include "CrashCatcher.h"
#include <sys/resource.h>
#endif

#include <validation/VerifyDB.h>
#include <utxo/UnspentOutputDatabase.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/function.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>


#if ENABLE_ZMQ
#include "zmq/zmqnotificationinterface.h"
#endif

#ifdef ENABLE_WALLET
CWallet* pwalletMain = NULL;
#endif

#if ENABLE_ZMQ
static CZMQNotificationInterface* pzmqNotificationInterface = NULL;
#endif

#ifdef WIN32
// Win32 LevelDB doesn't use filedescriptors, and the ones used for
// accessing block files don't count towards the fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

/** Used to pass flags to the Bind() function */
enum BindFlags {
    BF_NONE         = 0,
    BF_EXPLICIT     = (1U << 0),
    BF_REPORT_ERROR = (1U << 1),
    BF_WHITELIST    = (1U << 2),
};

CClientUIInterface uiInterface; // Declared but not defined in ui_interface.h

namespace {
/**
 * this function tries to raise the file descriptor limit to the requested number.
 * It returns the actual file descriptor limit (which may be more or less than nMinFD)
 */
int RaiseFileDescriptorLimit(int nMinFD) {
#if defined(WIN32)
    return 2048;
#else
    struct rlimit limitFD;
    if (getrlimit(RLIMIT_NOFILE, &limitFD) != -1) {
        if (limitFD.rlim_cur < (rlim_t)nMinFD) {
            limitFD.rlim_cur = nMinFD;
            if (limitFD.rlim_cur > limitFD.rlim_max)
                limitFD.rlim_cur = limitFD.rlim_max;
            setrlimit(RLIMIT_NOFILE, &limitFD);
            getrlimit(RLIMIT_NOFILE, &limitFD);
        }
        return limitFD.rlim_cur;
    }
    return nMinFD; // getrlimit failed, assume it's fine
#endif
}

#ifndef WIN32
boost::filesystem::path GetPidFile()
{
    boost::filesystem::path pathPidFile(GetArg("-pid", Settings::hubPidFilename()));
    if (!pathPidFile.is_complete()) pathPidFile = GetDataDir() / pathPidFile;
    return pathPidFile;
}

void CreatePidFile(const boost::filesystem::path &path, pid_t pid)
{
    FILE* file = fopen(path.string().c_str(), "w");
    if (file)
    {
        fprintf(file, "%d\n", pid);
        fclose(file);
    }
}
#endif

void ShrinkDebugFile()
{
    // Scroll hub.log if it's getting too big
    boost::filesystem::path pathLog = GetDataDir() / "hub.log";
    FILE* file = fopen(pathLog.string().c_str(), "r");
    if (file && boost::filesystem::file_size(pathLog) > 10 * 1000000)
    {
        // Restart the file with some of the end
        std::vector <char> vch(200000,0);
        fseek(file, -((long)vch.size()), SEEK_END);
        int nBytes = fread(begin_ptr(vch), 1, vch.size(), file);
        fclose(file);

        file = fopen(pathLog.string().c_str(), "w");
        if (file)
        {
            fwrite(begin_ptr(vch), 1, nBytes, file);
            fclose(file);
        }
    }
    else if (file != NULL)
        fclose(file);
}


}

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group
// created by AppInit() or the Qt main() function.
//
// A clean exit happens when StartShutdown() or the SIGTERM
// signal handler sets fRequestShutdown, which triggers
// the DetectShutdownThread(), which interrupts the main thread group.
// DetectShutdownThread() then exits, which causes AppInit() to
// continue (it .joins the shutdown thread).
// Shutdown() is then
// called to clean up database connections, and stop other
// threads that should only be stopped after the main network-processing
// threads have exited.
//
// Note that if running -daemon the parent process returns from AppInit2
// before adding any threads to the threadGroup, so .join_all() returns
// immediately and the parent exits from main().
//
// Shutdown for Qt is very similar, only it uses a QTimer to detect
// fRequestShutdown getting set, and then does the normal Qt
// shutdown thing.
//

volatile bool fRequestShutdown = false;

void StartShutdown()
{
    fRequestShutdown = true;
}
bool ShutdownRequested()
{
    return fRequestShutdown;
}

static boost::scoped_ptr<ECCVerifyHandle> globalVerifyHandle;

UnspentOutputDatabase *g_utxo = nullptr;

void Interrupt(boost::thread_group& threadGroup)
{
    InterruptHTTPServer();
    InterruptRPC();
    InterruptTorControl();
    threadGroup.interrupt_all();
}

void Shutdown()
{
    logCritical(Log::Bitcoin) << "Shutdown in progress...";
    static CCriticalSection cs_Shutdown;
    TRY_LOCK(cs_Shutdown, lockShutdown);
    if (!lockShutdown)
        return;

    /// Note: Shutdown() must be able to handle cases in which AppInit2() failed part of the way,
    /// for example if the data directory was found to be locked.
    /// Be sure that anything that writes files or flushes caches only does this if the respective
    /// module was initialized.
    RenameThread("hub-shutoff");
    mempool.AddTransactionsUpdated(1);

    StopHTTPRPC();
    StopREST();
    StopRPC();
    StopHTTPServer();
#ifdef ENABLE_WALLET
    if (pwalletMain)
        pwalletMain->Flush(false);
#endif
    Mining::Stop();
    StopNode();
    StopTorControl();
    UnregisterNodeSignals(GetNodeSignals());

    Application::quit(0);
    Application::exec(); // waits for threads to finish.

    {
        LOCK(cs_main);
        FlushStateToDisk();
        delete g_utxo;
        g_utxo = nullptr;
        Blocks::DB::shutdown();
    }
#ifdef ENABLE_WALLET
    if (pwalletMain)
        pwalletMain->Flush(true);
#endif

#if ENABLE_ZMQ
    if (pzmqNotificationInterface) {
        ValidationNotifier().removeListener(pzmqNotificationInterface);
        delete pzmqNotificationInterface;
        pzmqNotificationInterface = NULL;
    }
#endif

#ifndef WIN32
    try {
        boost::filesystem::remove(GetPidFile());
    } catch (const boost::filesystem::filesystem_error& e) {
        logCritical(Log::Bitcoin) << "Shutdown: Unable to remove pidfile:" <<  e;
    }
#endif
    ValidationNotifier().removeAll();
#ifdef ENABLE_WALLET
    delete pwalletMain;
    pwalletMain = NULL;
#endif
    globalVerifyHandle.reset();
    ECC_Stop();
    logCritical(Log::Bitcoin) << "Shutdown: done";
}

/**
 * Signal handlers are very limited in what they are allowed to do, so:
 */
void HandleSIGTERM(int)
{
    fRequestShutdown = true;
}

void HandleSIGHUP(int)
{
    Log::Manager::instance()->reopenLogFiles();
    Log::Manager::instance()->parseConfig(GetConfigFile("logs.conf"), GetDataDir(true) / "hub.log");
}

bool static InitError(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR);
    return false;
}

bool static InitWarning(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING);
    return true;
}

bool static Bind(const CService &addr, unsigned int flags) {
    if (!(flags & BF_EXPLICIT) && IsLimited(addr))
        return false;
    std::string strError;
    if (!BindListenPort(addr, strError, (flags & BF_WHITELIST) != 0)) {
        if (flags & BF_REPORT_ERROR)
            return InitError(strError);
        return false;
    }
    return true;
}

void OnRPCStopped()
{
    cvBlockChange.notify_all();
    logInfo(Log::RPC) << "RPC stopped.";
}

void OnRPCPreCommand(const CRPCCommand& cmd)
{
    // Observe safe mode
    std::string strWarning = GetWarnings("rpc");
    if (strWarning != "" && !GetBoolArg("-disablesafemode", Settings::DefaultDisableSafemode) &&
        !cmd.okSafeMode)
        throw JSONRPCError(RPC_FORBIDDEN_BY_SAFE_MODE, std::string("Safe mode: ") + strWarning);
}

static void BlockNotifyCallback(bool initialSync, const CBlockIndex *pBlockIndex)
{
    if (initialSync || !pBlockIndex)
        return;

    std::string strCmd = GetArg("-blocknotify", "");

    boost::replace_all(strCmd, "%s", pBlockIndex->GetBlockHash().GetHex());
    boost::thread t(runCommand, strCmd); // thread runs free
}


/** Sanity checks
 *  Ensure that Bitcoin is running in a usable environment with all
 *  necessary library support.
 */
bool InitSanityCheck(void)
{
    if(!ECC_InitSanityCheck()) {
        InitError("Elliptic curve cryptography sanity check failure. Aborting.");
        return false;
    }
    if (!glibc_sanity_test() || !glibcxx_sanity_test())
        return false;

    return true;
}

bool AppInitServers()
{
    RPCServer::OnStopped(&OnRPCStopped);
    RPCServer::OnPreCommand(&OnRPCPreCommand);
    if (!InitHTTPServer())
        return false;
    StartRPC();
    if (!StartHTTPRPC())
        return false;
    if (GetBoolArg("-rest", Settings::DefaultRestEnable))
        StartREST();
    StartHTTPServer();
    return true;
}

// Parameter interaction based on rules
void InitParameterInteraction()
{
    // when specifying an explicit binding address, you want to listen on it
    // even when -connect or -proxy is specified
    if (mapArgs.count("-bind")) {
        if (SoftSetBoolArg("-listen", true))
            logCritical(Log::Net) << "parameter interaction: -bind set -> setting -listen=1";
    }
    if (mapArgs.count("-whitebind")) {
        if (SoftSetBoolArg("-listen", true))
            logCritical(Log::Net) << "parameter interaction: -whitebind set -> setting -listen=1";
    }

    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (SoftSetBoolArg("-dnsseed", false))
            logCritical(Log::Net) << "parameter interaction: -connect set -> setting -dnsseed=0";
        if (SoftSetBoolArg("-listen", false))
            logCritical(Log::Net) << "parameter interaction: -connect set -> setting -listen=0";
    }

    if (mapArgs.count("-proxy")) {
        // to protect privacy, do not listen by default if a default proxy server is specified
        if (SoftSetBoolArg("-listen", false))
            logCritical(Log::Proxy) << "parameter interaction: -proxy set -> setting -listen=0";
        // to protect privacy, do not use UPNP when a proxy is set. The user may still specify -listen=1
        // to listen locally, so don't rely on this happening through -listen below.
        if (SoftSetBoolArg("-upnp", false))
            logCritical(Log::Proxy) << "parameter interaction: -proxy set -> setting -upnp=0";
        // to protect privacy, do not discover addresses by default
        if (SoftSetBoolArg("-discover", false))
            logCritical(Log::Proxy) << "parameter interaction: -proxy set -> setting -discover=0";
    }

    if (!GetBoolArg("-listen", DEFAULT_LISTEN)) {
        // do not map ports or try to retrieve public IP when not listening (pointless)
        if (SoftSetBoolArg("-upnp", false))
            logCritical(Log::Net) << "parameter interaction: -listen=0 -> setting -upnp=0";
        if (SoftSetBoolArg("-discover", false))
            logCritical(Log::Net) << "parameter interaction: -listen=0 -> setting -discover=0";
        if (SoftSetBoolArg("-listenonion", false))
            logCritical(Log::Net) << "parameter interaction: -listen=0 -> setting -listenonion=0";
    }

    if (mapArgs.count("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        if (SoftSetBoolArg("-discover", false))
            logCritical(Log::Net) << "parameter interaction: -externalip set -> setting -discover=false";
    }

    if (GetBoolArg("-salvagewallet", false)) {
        // Rewrite just private keys: rescan to find transactions
        if (SoftSetBoolArg("-rescan", true))
            logCritical(Log::Wallet) << "parameter interaction: -salvagewallet -> setting -rescan=true";
    }

    // -zapwallettx implies a rescan
    if (GetBoolArg("-zapwallettxes", false)) {
        if (SoftSetBoolArg("-rescan", true))
            logCritical(Log::Wallet) << "parameter interaction: -zapwallettxes=<mode> -> setting -rescan=true";
    }

    // disable walletbroadcast and whitelistrelay in blocksonly mode
    if (GetBoolArg("-blocksonly", Settings::DefaultBlocksOnly)) {
        if (SoftSetBoolArg("-whitelistrelay", false))
            logCritical(Log::Net) << "parameter interaction: -blocksonly=true -> setting -whitelistrelay=false";
#ifdef ENABLE_WALLET
        if (SoftSetBoolArg("-walletbroadcast", false))
            logCritical(Log::Wallet) << "parameter interaction: -blocksonly=1 -> setting -walletbroadcast=0";
#endif
    }

    // Forcing relay from whitelisted hosts implies we will accept relays from them in the first place.
    if (GetBoolArg("-whitelistforcerelay", Settings::DefaultWhitelistForceRelay)) {
        if (SoftSetBoolArg("-whitelistrelay", true))
            logCritical(Log::Net) << "parameter interaction: -whitelistforcerelay=true -> setting -whitelistrelay=true";
    }

    const auto miningSize = GetArg("-blockmaxsize", -1);
    if (miningSize > 0x7FFFFFFF) { // overflow
        logCritical(Log::Mining) << "parameter -blockmaxsize is too large. Max is 31bit int";
        throw std::runtime_error("invalid parameter passed to -blockmaxsize");
    }
    const int32_t acceptSize = Policy::blockSizeAcceptLimit();
    if ((int) miningSize > acceptSize) {
        if (SoftSetArg("-blocksizeacceptlimit", boost::lexical_cast<std::string>((miningSize + 100000) / 1E6)))
            logCritical(Log::Net) << "parameter interaction: -blockmaxsize  N -> setting -blockacceptlimit=N";
        else
            throw std::runtime_error("Block Accept setting smaller than block mining size. Please adjust and restart");
    }
    assert(Policy::blockSizeAcceptLimit() >= miningSize);

    const int64_t mempoolMaxSize = GetArg("-maxmempool", Settings::DefaultMaxMempoolSize) * 4500000;
    if (mempoolMaxSize < miningSize * 4) {
        if (SoftSetArg("-maxmempool", boost::lexical_cast<std::string>(miningSize * 4)))
            logCritical(Log::Net) << "parameter interaction: -blockmaxsize  N -> setting -maxmempool=4N";
    }

}

void InitLogging()
{
    fLogIPs = GetBoolArg("-logips", DEFAULT_LOGIPS);

    Log::Manager::instance()->parseConfig(GetConfigFile("logs.conf"), GetDataDir(true) / "hub.log");
    logCritical(Log::Bitcoin) << "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n";
    logCritical(Log::Bitcoin) << "Flowee the Hub version " << CLIENT_BUILD.c_str()
                              << "Built:" << CLIENT_DATE.c_str();
}

/** Initialize bitcoin.
 *  @pre Parameters should be parsed and config file should be read.
 */
bool AppInit2(boost::thread_group& threadGroup, CScheduler& scheduler)
{
    // ********************************************************* Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0));
#endif
#if _MSC_VER >= 1400
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
    // Enable Data Execution Prevention (DEP)
    // Minimum supported OS versions: WinXP SP3, WinVista >= SP1, Win Server 2008
    // A failure is non-critical and needs no further attention!
#ifndef PROCESS_DEP_ENABLE
    // We define this here, because GCCs winbase.h limits this to _WIN32_WINNT >= 0x0601 (Windows 7),
    // which is not correct. Can be removed, when GCCs winbase.h is fixed!
#define PROCESS_DEP_ENABLE 0x00000001
#endif
    typedef BOOL (WINAPI *PSETPROCDEPPOL)(DWORD);
    PSETPROCDEPPOL setProcDEPPol = (PSETPROCDEPPOL)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetProcessDEPPolicy");
    if (setProcDEPPol != NULL) setProcDEPPol(PROCESS_DEP_ENABLE);
#endif


    if (!SetupNetworking())
        return InitError("Initializing networking failed");

#ifndef WIN32

    // Clean shutdown on SIGTERM
    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Reopen hub.log on SIGHUP
    struct sigaction sa_hup;
    sa_hup.sa_handler = HandleSIGHUP;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;
    sigaction(SIGHUP, &sa_hup, NULL);

    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    signal(SIGPIPE, SIG_IGN);

    if (GetBoolArg("catch-crash", false))
        setupBacktraceCatcher();
#endif

    // ********************************************************* Step 2: parameter interactions
    const CChainParams& chainparams = Params();

    // also see: InitParameterInteraction()

    // Make sure enough file descriptors are available
    int nBind = std::max((int)mapArgs.count("-bind") + (int)mapArgs.count("-whitebind"), 1);
    int nUserMaxConnections = GetArg("-maxconnections", Settings::DefaultMaxPeerConnections);
    nMaxConnections = std::max(nUserMaxConnections, 0);

    // Trim requested connection counts, to fit into system limitations
    nMaxConnections = std::max(std::min(nMaxConnections, (int)(FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS)), 0);
    int nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS);
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
        return InitError(_("Not enough file descriptors available."));
    nMaxConnections = std::min(nFD - MIN_CORE_FILEDESCRIPTORS, nMaxConnections);

    if (nMaxConnections < nUserMaxConnections)
        InitWarning(strprintf(_("Reducing -maxconnections from %d to %d, because of system limitations."), nUserMaxConnections, nMaxConnections));

    // ********************************************************* Step 3: parameter-to-internal-flags

    fCheckpointsEnabled = GetBoolArg("-checkpoints", Settings::DefaultCheckpointsEnabled);

    // mempool limits
    int64_t nMempoolSizeMax = GetArg("-maxmempool", Settings::DefaultMaxMempoolSize) * 1000000;
    int64_t nMempoolSizeMin = GetArg("-limitdescendantsize", Settings::DefaultDescendantSizeLimit) * 1000 * 40;
    if (nMempoolSizeMax < 0 || nMempoolSizeMax < nMempoolSizeMin)
        return InitError(strprintf(_("-maxmempool must be at least %d MB"), std::ceil(nMempoolSizeMin / 1000000.0)));

    fServer = GetBoolArg("-server", false);

#ifdef ENABLE_WALLET
    bool fDisableWallet = GetBoolArg("-disablewallet", false);
#endif

    nConnectTimeout = GetArg("-timeout", Settings::DefaultConnectTimeout);
    if (nConnectTimeout <= 0)
        nConnectTimeout = Settings::DefaultConnectTimeout;

    // Fee-per-kilobyte amount considered the same as "free"
    // If you are mining, be careful setting this:
    // if you set it to zero then
    // a transaction spammer can cheaply fill blocks using
    // 1-satoshi-fee transactions. It should be set above the real
    // cost to you of processing a transaction.
    if (mapArgs.count("-minrelaytxfee"))
    {
        CAmount n = 0;
        if (ParseMoney(mapArgs["-minrelaytxfee"], n) && n > 0) {
            logCritical(Log::Bitcoin) << "Setting min relay Transaction fee to" << n << "satoshi";
            ::minRelayTxFee = CFeeRate(n);
        } else {
            return InitError(strprintf(_("Invalid amount for -minrelaytxfee=<amount>: '%s'"), mapArgs["-minrelaytxfee"]));
        }
    }

    fRequireStandard = !GetBoolArg("-acceptnonstdtxn", !Params().RequireStandard());
    if (Params().RequireStandard() && !fRequireStandard)
        return InitError(strprintf("acceptnonstdtxn is not currently supported for %s chain", chainparams.NetworkIDString()));
    nBytesPerSigOp = GetArg("-bytespersigop", nBytesPerSigOp);

#ifdef ENABLE_WALLET
    if (mapArgs.count("-mintxfee"))
    {
        CAmount n = 0;
        if (ParseMoney(mapArgs["-mintxfee"], n) && n > 0)
            CWallet::minTxFee = CFeeRate(n);
        else
            return InitError(strprintf(_("Invalid amount for -mintxfee=<amount>: '%s'"), mapArgs["-mintxfee"]));
    }
    if (mapArgs.count("-fallbackfee"))
    {
        CAmount nFeePerK = 0;
        if (!ParseMoney(mapArgs["-fallbackfee"], nFeePerK))
            return InitError(strprintf(_("Invalid amount for -fallbackfee=<amount>: '%s'"), mapArgs["-fallbackfee"]));
        if (nFeePerK > nHighTransactionFeeWarning)
            InitWarning(_("-fallbackfee is set very high! This is the transaction fee you may pay when fee estimates are not available."));
        CWallet::fallbackFee = CFeeRate(nFeePerK);
    }
    if (mapArgs.count("-paytxfee"))
    {
        CAmount nFeePerK = 0;
        if (!ParseMoney(mapArgs["-paytxfee"], nFeePerK))
            return InitError(strprintf(_("Invalid amount for -paytxfee=<amount>: '%s'"), mapArgs["-paytxfee"]));
        if (nFeePerK > nHighTransactionFeeWarning)
            InitWarning(_("-paytxfee is set very high! This is the transaction fee you will pay if you send a transaction."));
        payTxFee = CFeeRate(nFeePerK, 1000);
        if (payTxFee < ::minRelayTxFee)
        {
            return InitError(strprintf(_("Invalid amount for -paytxfee=<amount>: '%s' (must be at least %s)"),
                                       mapArgs["-paytxfee"], ::minRelayTxFee.ToString()));
        }
    }
    if (mapArgs.count("-maxtxfee"))
    {
        CAmount nMaxFee = 0;
        if (!ParseMoney(mapArgs["-maxtxfee"], nMaxFee))
            return InitError(strprintf(_("Invalid amount for -maxtxfee=<amount>: '%s'"), mapArgs["-maxtxfee"]));
        if (nMaxFee > nHighTransactionMaxFeeWarning)
            InitWarning(_("-maxtxfee is set very high! Fees this large could be paid on a single transaction."));
        maxTxFee = nMaxFee;
        if (CFeeRate(maxTxFee, 1000) < ::minRelayTxFee)
        {
            return InitError(strprintf(_("Invalid amount for -maxtxfee=<amount>: '%s' (must be at least the minrelay fee of %s to prevent stuck transactions)"),
                                       mapArgs["-maxtxfee"], ::minRelayTxFee.ToString()));
        }
    }
    nTxConfirmTarget = GetArg("-txconfirmtarget", Settings::defaultTxConfirmTarget);
    bSpendZeroConfChange = GetBoolArg("-spendzeroconfchange", Settings::DefaultSpendZeroconfChange);
    fSendFreeTransactions = GetBoolArg("-sendfreetransactions", Settings::DefaultSendFreeTransactions);

    std::string strWalletFile = GetArg("-wallet", "wallet.dat");
#endif // ENABLE_WALLET

    fIsBareMultisigStd = GetBoolArg("-permitbaremultisig", Settings::DefaultPermitBareMultisig);
    fAcceptDatacarrier = GetBoolArg("-datacarrier", Settings::DefaultAcceptDataCarrier);
    nMaxDatacarrierBytes = GetArg("-datacarriersize", nMaxDatacarrierBytes);

    // Option to startup with mocktime set (used for regression testing):
    SetMockTime(GetArg("-mocktime", 0)); // SetMockTime(0) is a no-op

    if (GetBoolArg("-peerbloomfilters", true))
        nLocalServices |= NODE_BLOOM;

    if (GetBoolArg("-use-thinblocks", false))
        nLocalServices |= NODE_XTHIN;
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (Policy::blockSizeAcceptLimit() < 8000000)
            return InitError("The block size accept limit is too low, the minimum is 8MB. The Hub is shutting down.");
        if (GetArg("-blockmaxsize", Settings::DefaultBlockMAxSize) <= 1000000)
            return InitError("The maxblocksize mining limit is too low, it should be over 1MB. The Hub is shutting down.");
    }
    else if (Params().NetworkIDString() == CBaseChainParams::REGTEST) { // setup for testing to not use so much disk space.
        UnspentOutputDatabase::setSmallLimits();
    }

    // ********************************************************* Step 4: application initialization: dir lock, daemonize, pidfile, hub log

    // Initialize elliptic curve code
    ECC_Start();
    globalVerifyHandle.reset(new ECCVerifyHandle());

    // Initialize SigCache
    InitSignatureCache();

    // Sanity check
    if (!InitSanityCheck())
        return InitError(_("Initialization sanity check failed. The Hub is shutting down."));

    std::string strDataDir = GetDataDir().string();
#ifdef ENABLE_WALLET
    // Wallet file must be a plain filename without a directory
    if (strWalletFile != boost::filesystem::basename(strWalletFile) + boost::filesystem::extension(strWalletFile))
        return InitError(strprintf(_("Wallet %s resides outside data directory %s"), strWalletFile, strDataDir));
#endif
    // Make sure only a single Bitcoin process is using the data directory.
    boost::filesystem::path pathLockFile = GetDataDir() / ".lock";
    FILE* file = fopen(pathLockFile.string().c_str(), "a"); // empty lock file; created if it doesn't exist.
    if (file) fclose(file);

    try {
        static boost::interprocess::file_lock lock(pathLockFile.string().c_str());
        if (!lock.try_lock())
            return InitError(strprintf(_("Cannot obtain a lock on data directory %s. The Hub is probably already running."), strDataDir));
    } catch(const boost::interprocess::interprocess_exception& e) {
        return InitError(strprintf(_("Cannot obtain a lock on data directory %s. The Hub is probably already running.") + " %s.", strDataDir, e.what()));
    }

#ifndef WIN32
    CreatePidFile(GetPidFile(), getpid());
#endif
    if (GetBoolArg("-shrinkdebugfile", true))
        ShrinkDebugFile();

#ifdef ENABLE_WALLET
    logCritical(Log::Wallet) << "Using BerkeleyDB version" << DbEnv::version(0, 0, 0);
#endif
    logCritical(Log::Bitcoin) << "Startup time:" << DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime());
    logCritical(Log::Bitcoin) << "Using data directory" << strDataDir;
    logCritical(Log::Bitcoin) << "Using config file" << GetConfigFile().string();
    logCritical(Log::Bitcoin) << "Using log-config file" << GetConfigFile("logs.conf").string();
    logInfo(Log::Net) << "Using at most" << nMaxConnections  << "connections.";
    logInfo(Log::Internals) << nFD << "file descriptors available";
    std::ostringstream strErrors;

    // Start the lightweight task scheduler thread
    CScheduler::Function serviceLoop = boost::bind(&CScheduler::serviceQueue, &scheduler);
    threadGroup.create_thread(boost::bind(&TraceThread<CScheduler::Function>, "scheduler", serviceLoop));

    /* Start the RPC server already.  It will be started in "warmup" mode
     * and not really process calls already (but it will signify connections
     * that the server is there and will be ready later).  Warmup mode will
     * be disabled when initialisation is finished.
     */
    if (fServer)
    {
        uiInterface.InitMessage.connect(SetRPCWarmupStatus);
        if (!AppInitServers())
            return InitError(_("Unable to start HTTP server. See hub log for details."));
    }

    Application::instance()->validation()->setMempool(&mempool);

    // ********************************************************* Step 5: verify wallet database integrity
#ifdef ENABLE_WALLET
    if (!fDisableWallet) {
        logCritical(Log::Wallet) << "Using wallet" << strWalletFile;
        uiInterface.InitMessage(_("Verifying wallet..."));

        std::string warningString;
        std::string errorString;

        if (!CWallet::Verify(strWalletFile, warningString, errorString))
            return false;

        if (!warningString.empty())
            InitWarning(warningString);
        if (!errorString.empty())
            return InitError(errorString);

    } // (!fDisableWallet)
#endif // ENABLE_WALLET


    // ********************************************************* Step 6: load block chain

    bool fReindex = GetBoolArg("-reindex", false);
    bool fLoaded = false;
    int64_t nStart;
    while (!fLoaded) {
        bool fReset = fReindex;
        std::string strLoadError;

        uiInterface.InitMessage(_("Loading block index..."));

        nStart = GetTimeMillis();
        do {
            try {
                UnloadBlockIndex();
                delete g_utxo;
                g_utxo = nullptr;
                Blocks::DB::createInstance(40 << 20, fReindex, &scheduler);
                const auto utxoDir = GetDataDir() / "unspent";
                if (fReindex) {
                    boost::system::error_code error;
                    boost::filesystem::remove_all(utxoDir, error);
                    if (error) {
                        fRequestShutdown = true;
                        logFatal(Log::Bitcoin) << "Can't remove the unspent dir to do a reindex" << error.message();
                        break;
                    }
                }
                g_utxo = new UnspentOutputDatabase(Application::instance()->ioService(), utxoDir);
                mempool.setUtxo(g_utxo);
                if (fReindex)
                    Blocks::DB::instance()->setReindexing(Blocks::ScanningFiles);

                if (!fReindex && !LoadBlockIndexDB()) {
                    strLoadError = _("Error loading block database");
                    break;
                }
                if (!fReindex && g_utxo->blockheight() == 0 && Blocks::Index::size() > 1) {
                    // We have block-indexes, but we have no UTXO. This means we need to reindex.
                    fRequestShutdown = true;
                    logFatal(Log::Bitcoin) << "This version uses a new UTXO format, you need to restart with -reindex";
                    break;
                }
                Application::instance()->validation()->setBlockchain(&chainActive);
                scheduler.scheduleEvery(std::bind(&UnspentOutputDatabase::saveCaches, g_utxo), 5 * 60);

                // Check whether we need to continue reindexing
                fReindex = fReindex || Blocks::DB::instance()->reindexing() != Blocks::NoReindex;

                // If the loaded chain has a wrong genesis, bail out immediately
                // (we're likely using a testnet datadir, or the other way around).
                if (!Blocks::Index::empty() && !Blocks::Index::exists(chainparams.GetConsensus().hashGenesisBlock))
                    return InitError(_("Incorrect or no genesis block found. Wrong datadir for network?"));

                // Initialize the block index (no-op if non-empty database was already loaded)
                if (!InitBlockIndex(chainparams)) {
                    strLoadError = _("Error initializing block database");
                    break;
                }

                uiInterface.InitMessage(_("Verifying blocks..."));
                {
                    LOCK(cs_main);
                    CBlockIndex* tip = chainActive.Tip();
                    if (tip && tip->nTime > GetAdjustedTime() + 2 * 60 * 60) {
                        strLoadError = _("The block database contains a block which appears to be from the future. "
                                "This may be due to your computer's date and time being set incorrectly. "
                                "Only rebuild the block database if you are sure that your computer's date and time are correct");
                        break;
                    }
                }

                if (!VerifyDB().verifyDB(GetArg("-checklevel", Settings::DefaultCheckLevel),
                              GetArg("-checkblocks", Settings::DefaultCheckBlocks))) {
                    strLoadError = _("Corrupted block database detected");
                    break;
                }
            } catch (const std::exception& e) {
                logWarning() << e;
                strLoadError = _("Error opening block database");
                break;
            }

            fLoaded = true;
        } while(false);

        if (fRequestShutdown)
            break;

        if (!fLoaded) {
            // first suggest a reindex
            if (!fReset) {
                bool fRet = uiInterface.ThreadSafeMessageBox(
                    strLoadError + ".\n\n" + _("Do you want to rebuild the block database now?"),
                    "", CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
                if (fRet) {
                    fReindex = true;
                    Blocks::DB::instance()->setReindexing(Blocks::ScanningFiles);
                    fRequestShutdown = false;
                } else {
                    logFatal(Log::Bitcoin) << "Aborted block database rebuild. Exiting.";
                    return false;
                }
            } else {
                return InitError(strLoadError);
            }
        }
    }

    // As LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (fRequestShutdown)
    {
        logFatal(Log::Bitcoin) << "Shutdown requested. Exiting.";
        return false;
    }
    logInfo(Log::Bench).nospace() << "block index load took: " << GetTimeMillis() - nStart << "ms";


    // ********************************************************* Step 7: network initialization

    CTxOrphanCache::instance()->setLimit((unsigned int)std::max((int64_t)0, GetArg("-maxorphantx", Settings::DefaultMaxOrphanTransactions)));

    RegisterNodeSignals(GetNodeSignals());

    if (mapArgs.count("-onlynet")) {
        std::set<CNetAddr::Network> nets;
        for (const std::string& snet : mapMultiArgs["-onlynet"]) {
            CNetAddr::Network net = ParseNetwork(snet);
            if (net == CNetAddr::NET_UNROUTABLE)
                return InitError(strprintf(_("Unknown network specified in -onlynet: '%s'"), snet));
            nets.insert(net);
        }
        for (int n = 0; n < CNetAddr::NET_MAX; n++) {
            CNetAddr::Network net = (CNetAddr::Network)n;
            if (!nets.count(net))
                SetLimited(net);
        }
    }

    if (mapArgs.count("-whitelist")) {
        for (const std::string& net : mapMultiArgs["-whitelist"]) {
            CSubNet subnet(net);
            if (!subnet.IsValid())
                return InitError(strprintf(_("Invalid netmask specified in -whitelist: '%s'"), net));
            CNode::AddWhitelistedRange(subnet);
        }
    }

    bool proxyRandomize = GetBoolArg("-proxyrandomize", Settings::DefaultProxyRandomize);
    // -proxy sets a proxy for all outgoing network traffic
    // -noproxy (or -proxy=0) as well as the empty string can be used to not set a proxy, this is the default
    std::string proxyArg = GetArg("-proxy", "");
    SetLimited(CNetAddr::NET_TOR);
    if (proxyArg != "" && proxyArg != "0") {
        proxyType addrProxy = proxyType(CService(proxyArg, 9050), proxyRandomize);
        if (!addrProxy.IsValid())
            return InitError(strprintf(_("Invalid -proxy address: '%s'"), proxyArg));

        SetProxy(CNetAddr::NET_IPV4, addrProxy);
        SetProxy(CNetAddr::NET_IPV6, addrProxy);
        SetProxy(CNetAddr::NET_TOR, addrProxy);
        SetNameProxy(addrProxy);
        SetLimited(CNetAddr::NET_TOR, false); // by default, -proxy sets onion as reachable, unless -noonion later
    }

    // -onion can be used to set only a proxy for .onion, or override normal proxy for .onion addresses
    // -noonion (or -onion=0) disables connecting to .onion entirely
    // An empty string is used to not override the onion proxy (in which case it defaults to -proxy set above, or none)
    std::string onionArg = GetArg("-onion", "");
    if (onionArg != "") {
        if (onionArg == "0") { // Handle -noonion/-onion=0
            SetLimited(CNetAddr::NET_TOR); // set onions as unreachable
        } else {
            proxyType addrOnion = proxyType(CService(onionArg, 9050), proxyRandomize);
            if (!addrOnion.IsValid())
                return InitError(strprintf(_("Invalid -onion address: '%s'"), onionArg));
            SetProxy(CNetAddr::NET_TOR, addrOnion);
            SetLimited(CNetAddr::NET_TOR, false);
        }
    }

    // see Step 2: parameter interactions for more information about these
    fListen = GetBoolArg("-listen", DEFAULT_LISTEN);
    fDiscover = GetBoolArg("-discover", true);
    fNameLookup = GetBoolArg("-dns", Settings::DefaultNameLookup);

    bool fBound = false;
    if (fListen) {
        if (mapArgs.count("-bind") || mapArgs.count("-whitebind")) {
            for (const std::string& strBind : mapMultiArgs["-bind"]) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false))
                    return InitError(strprintf(_("Cannot resolve -bind address: '%s'"), strBind));
                fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR));
            }
            for (const std::string& strBind : mapMultiArgs["-whitebind"]) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, 0, false))
                    return InitError(strprintf(_("Cannot resolve -whitebind address: '%s'"), strBind));
                if (addrBind.GetPort() == 0)
                    return InitError(strprintf(_("Need to specify a port with -whitebind: '%s'"), strBind));
                fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR | BF_WHITELIST));
            }
        }
        else {
            struct in_addr inaddr_any;
            inaddr_any.s_addr = INADDR_ANY;
            fBound |= Bind(CService(in6addr_any, GetListenPort()), BF_NONE);
            fBound |= Bind(CService(inaddr_any, GetListenPort()), !fBound ? BF_REPORT_ERROR : BF_NONE);
        }
        if (!fBound)
            return InitError(_("Failed to listen on any port. Use -listen=0 if you want this."));
    }

    if (mapArgs.count("-externalip")) {
        for (const std::string& strAddr : mapMultiArgs["-externalip"]) {
            CService addrLocal(strAddr, GetListenPort(), fNameLookup);
            if (!addrLocal.IsValid())
                return InitError(strprintf(_("Cannot resolve -externalip address: '%s'"), strAddr));
            AddLocal(CService(strAddr, GetListenPort(), fNameLookup), LOCAL_MANUAL);
        }
    }

    for (const std::string& strDest : mapMultiArgs["-seednode"])
        AddOneShot(strDest);

#if ENABLE_ZMQ
    pzmqNotificationInterface = CZMQNotificationInterface::CreateWithArguments(mapArgs);

    if (pzmqNotificationInterface) {
        ValidationNotifier().addListener(pzmqNotificationInterface);
    }
#endif
    if (mapArgs.count("-maxuploadtarget")) {
        CNode::SetMaxOutboundTarget(GetArg("-maxuploadtarget", Settings::DefaultMaxUploadTarget)*1024*1024);
    }


    // ********************************************************* Step 8: load wallet
#ifdef ENABLE_WALLET
    if (fDisableWallet) {
        pwalletMain = NULL;
        logInfo(Log::Wallet) << "Wallet disabled!";
    } else {
        // needed to restore wallet transaction meta data after -zapwallettxes
        std::vector<CWalletTx> vWtx;

        if (GetBoolArg("-zapwallettxes", false)) {
            uiInterface.InitMessage(_("Zapping all transactions from wallet..."));

            pwalletMain = new CWallet(strWalletFile);
            DBErrors nZapWalletRet = pwalletMain->ZapWalletTx(vWtx);
            if (nZapWalletRet != DB_LOAD_OK) {
                uiInterface.InitMessage(_("Error loading wallet.dat: Wallet corrupted"));
                return false;
            }
            delete pwalletMain;
            pwalletMain = NULL;
        }

        uiInterface.InitMessage(_("Loading wallet..."));
        nStart = GetTimeMillis();
        bool fFirstRun = true;
        pwalletMain = new CWallet(strWalletFile);
        DBErrors nLoadWalletRet = pwalletMain->LoadWallet(fFirstRun);
        if (nLoadWalletRet != DB_LOAD_OK)
        {
            if (nLoadWalletRet == DB_CORRUPT)
                strErrors << _("Error loading wallet.dat: Wallet corrupted") << "\n";
            else if (nLoadWalletRet == DB_NONCRITICAL_ERROR)
            {
                InitWarning(_("Error reading wallet.dat! All keys read correctly, but transaction data"
                             " or address book entries might be missing or incorrect."));
            }
            else if (nLoadWalletRet == DB_TOO_NEW)
                strErrors << _("Error loading wallet.dat: Wallet requires newer version of The Hub") << "\n";
            else if (nLoadWalletRet == DB_NEED_REWRITE)
            {
                strErrors << _("Wallet needed to be rewritten: restart The Hub to complete") << "\n";
                logFatal(Log::Wallet) << strErrors.str();
                return InitError(strErrors.str());
            }
            else
                strErrors << _("Error loading wallet.dat") << "\n";
        }

        if (fFirstRun) {
            pwalletMain->SetMinVersion(FEATURE_LATEST);

            // Create new keyUser and set as default key
            RandAddSeedPerfmon();

            CPubKey newDefaultKey;
            if (pwalletMain->GetKeyFromPool(newDefaultKey)) {
                pwalletMain->SetDefaultKey(newDefaultKey);
                if (!pwalletMain->SetAddressBook(pwalletMain->vchDefaultKey.GetID(), "", "receive"))
                    strErrors << _("Cannot write default address") << "\n";
            }

            pwalletMain->SetBestChain(chainActive.GetLocator());
        }

        if (!strErrors.str().empty())
            logFatal(Log::Wallet) << strErrors.str();
        logInfo(Log::Wallet).nospace() << "wallet load took: " << GetTimeMillis() - nStart << "ms";

        ValidationNotifier().addListener(pwalletMain);

        CBlockIndex *pindexRescan = chainActive.Tip();
        if (GetBoolArg("-rescan", false)) {
            pindexRescan = chainActive.Genesis();
        } else {
            CWalletDB walletdb(strWalletFile);
            CBlockLocator locator;
            if (walletdb.ReadBestBlock(locator))
                pindexRescan = FindForkInGlobalIndex(chainActive, locator);
            else
                pindexRescan = chainActive.Genesis();
        }
        if (chainActive.Tip() && chainActive.Tip() != pindexRescan) {
            uiInterface.InitMessage(_("Rescanning..."));
            logCritical(Log::Bitcoin) << "Rescanning last" << chainActive.Height() - pindexRescan->nHeight << "blocks. (from block"
                        << pindexRescan->nHeight << ")...";
            nStart = GetTimeMillis();
            pwalletMain->ScanForWalletTransactions(pindexRescan, true);
            logInfo(Log::Bench).nospace() << "rescan took: " << GetTimeMillis() - nStart << "ms";
            pwalletMain->SetBestChain(chainActive.GetLocator());
            nWalletDBUpdated++;

            // Restore wallet transaction metadata after -zapwallettxes=1
            if (GetBoolArg("-zapwallettxes", false) && GetArg("-zapwallettxes", "1") != "2") {
                CWalletDB walletdb(strWalletFile);

                for (const CWalletTx& wtxOld : vWtx) {
                    uint256 hash = wtxOld.GetHash();
                    auto mi = pwalletMain->mapWallet.find(hash);
                    if (mi != pwalletMain->mapWallet.end()) {
                        const CWalletTx* copyFrom = &wtxOld;
                        CWalletTx* copyTo = &mi->second;
                        copyTo->mapValue = copyFrom->mapValue;
                        copyTo->vOrderForm = copyFrom->vOrderForm;
                        copyTo->nTimeReceived = copyFrom->nTimeReceived;
                        copyTo->nTimeSmart = copyFrom->nTimeSmart;
                        copyTo->fFromMe = copyFrom->fFromMe;
                        copyTo->strFromAccount = copyFrom->strFromAccount;
                        copyTo->nOrderPos = copyFrom->nOrderPos;
                        copyTo->WriteToDisk(&walletdb);
                    }
                }
            }
        }
        pwalletMain->SetBroadcastTransactions(GetBoolArg("-walletbroadcast", Settings::DefaultWalletBroadcast));
    } // (!fDisableWallet)
#else // ENABLE_WALLET
    logDebug(Log::Wallet) << "No wallet support compiled in!";
#endif // !ENABLE_WALLET

    // ********************************************************* Step 9: import blocks

    if (mapArgs.count("-blocknotify"))
        uiInterface.NotifyBlockTip.connect(BlockNotifyCallback);

    Blocks::DB::startBlockImporter();
    if (chainActive.Tip() == nullptr) {
        logDebug(Log::Bitcoin) << "Waiting for genesis block to be imported...";
        while (!fRequestShutdown && chainActive.Tip() == nullptr)
            MilliSleep(10);
    }
    Application::instance()->validation()->start();

    // ********************************************************* Step 10: start node

    if (!CheckDiskSpace())
        return false;

    if (!strErrors.str().empty())
        return InitError(strErrors.str());

    RandAddSeedPerfmon();

    //// debug print
    logDebug(Log::DB) << "mapBlockIndex.size() =" << Blocks::Index::size();
    logDebug(Log::BlockValidation) << "nBestHeight =" << chainActive.Height();
#ifdef ENABLE_WALLET
    logDebug(Log::Wallet) << "setKeyPool.size() =" << (pwalletMain ? pwalletMain->setKeyPool.size() : 0);
    logDebug(Log::Wallet) << "mapWallet.size() =" << (pwalletMain ? pwalletMain->mapWallet.size() : 0);
    logDebug(Log::Wallet) << "mapAddressBook.size() =" << (pwalletMain ? pwalletMain->mapAddressBook.size() : 0);
#endif

    if (GetBoolArg("-listenonion", Settings::DefaultListenOnion))
        StartTorControl(threadGroup, scheduler);

    StartNode(threadGroup, scheduler);

    // Monitor the chain, and alert if we get blocks much quicker or slower than expected
    int64_t nPowTargetSpacing = Params().GetConsensus().nPowTargetSpacing;
    CScheduler::Function f = boost::bind(&PartitionCheck, &IsInitialBlockDownload,
                                         boost::ref(cs_main), boost::cref(pindexBestHeader), nPowTargetSpacing);
    scheduler.scheduleEvery(f, nPowTargetSpacing);

    // Generate coins in the background
    try {
        Mining::GenerateBitcoins(GetBoolArg("-gen", Settings::DefaultGenerateCoins), GetArg("-genproclimit", Settings::DefaultGenerateThreads),
                                 chainparams, GetArg("-gencoinbase", ""));
    } catch (const std::exception &e) {
        logCritical(Log::Bitcoin) << "Mining could not be activated. Reason: %s" << e.what();
    }

    // ********************************************************* Step 11: finished

    SetRPCWarmupFinished();
    uiInterface.InitMessage(_("Done loading"));

#ifdef ENABLE_WALLET
    if (pwalletMain) {
        // Add wallet transactions that aren't already in a block to mapTransactions
        pwalletMain->ReacceptWalletTransactions();

        // Run a thread to flush wallet periodically
        threadGroup.create_thread(boost::bind(&ThreadFlushWalletDB, boost::ref(pwalletMain->strWalletFile)));
    }
#endif

    return !fRequestShutdown;
}
