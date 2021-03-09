/*
 * This file is part of the Flowee project
 * Copyright (C) 2017 Stephen McCarthy
 * Copyright (C) 2017-2020 Tom Zander <tomz@freedommail.ch>
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

#include "allowed_args.h"
#include <SettingsDefaults.h>

#include "chainparamsbase.h"
#include "util.h" // for translate _()
#include "utilmoneystr.h"
#include "utilstrencodings.h"

#include <set>

namespace Settings {

enum HelpMessageMode {
    HMM_HUB,
    HMM_HUB_QT
};

static const int screenWidth = 79;
static const int optIndent = 2;
static const int msgIndent = 7;

std::string HelpMessageGroup(const std::string &message)
{
    return std::string(message) + std::string("\n\n");
}

std::string HelpMessageOpt(const std::string &option, const std::string &message)
{
    return std::string(optIndent, ' ') + std::string(option) +
           std::string("\n") + std::string(msgIndent, ' ') +
           FormatParagraph(message, screenWidth - msgIndent, msgIndent) +
           std::string("\n\n");
}

AllowedArgs& AllowedArgs::addHeader(const std::string& strHeader, bool debug)
{

    m_helpList.push_back(HelpComponent{strHeader + "\n\n", debug});
    return *this;
}

AllowedArgs& AllowedArgs::addDebugArg(const std::string& strArgsDefinition, CheckValueFunc checkValueFunc, const std::string& strHelp)
{
    return addArg(strArgsDefinition, checkValueFunc, strHelp, true);
}

AllowedArgs& AllowedArgs::addArg(const std::string& strArgsDefinition, CheckValueFunc checkValueFunc, const std::string& strHelp, bool debug)
{
    std::string strArgs = strArgsDefinition;
    std::string strExampleValue;
    size_t is_index = strArgsDefinition.find('=');
    if (is_index != std::string::npos) {
        strExampleValue = strArgsDefinition.substr(is_index + 1);
        strArgs = strArgsDefinition.substr(0, is_index);
    }

    if (strArgs == "")
        strArgs = ",";

    std::stringstream streamArgs(strArgs);
    std::string strArg;
    bool firstArg = true;
    while (std::getline(streamArgs, strArg, ',')) {
        m_args[strArg] = checkValueFunc;

        std::string optionText = std::string(optIndent, ' ') + "-" + strArg;
        if (!strExampleValue.empty())
            optionText += "=" + strExampleValue;
        optionText += "\n";
        m_helpList.push_back(HelpComponent{optionText, debug || !firstArg});

        firstArg = false;
    }

    std::string helpText = std::string(msgIndent, ' ') + FormatParagraph(strHelp, screenWidth - msgIndent, msgIndent) + "\n\n";
    m_helpList.push_back(HelpComponent{helpText, debug});

    return *this;
}

void AllowedArgs::checkArg(const std::string& strArg, const std::string& strValue) const
{
    if (!m_args.count(strArg))
        throw std::runtime_error(strprintf(_("unrecognized option '%s'"), strArg));

    if (!m_args.at(strArg)(strValue))
        throw std::runtime_error(strprintf(_("invalid value '%s' for option '%s'"), strValue, strArg));
}

std::string AllowedArgs::helpMessage() const
{
    const bool showDebug = GetBoolArg("-help-debug", false);
    std::string helpMessage;

    for (const HelpComponent &helpComponent : m_helpList)
        if (showDebug || !helpComponent.debug)
            helpMessage += helpComponent.text;

    return helpMessage;
}

//////////////////////////////////////////////////////////////////////////////
//
// CheckValueFunc functions
//

static const std::set<std::string> boolStrings{"", "1", "0", "t", "f", "y", "n", "true", "false", "yes", "no"};
static const std::set<char> intChars{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
static const std::set<char> amountChars{'.', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};

static bool validateString(const std::string &str, const std::set<char> &validChars)
{
    for (const char& c : str)
        if (!validChars.count(c))
            return false;
    return true;
}

static bool optionalBool(const std::string &str)
{
    return (boolStrings.count(str) != 0);
}

static bool requiredStr(const std::string &str)
{
    return !str.empty();
}

static bool optionalStr(const std::string&)
{
    return true;
}

static bool requiredInt(const std::string &str)
{
    if (str.empty() || str == "-")
        return false;

    // Allow the first character to be '-', to allow negative numbers.
    return validateString(str[0] == '-' ? str.substr(1) : str, intChars);
}

static bool optionalInt(const std::string &str)
{
    if (str.empty())
        return true;
    return requiredInt(str);
}

static bool requiredAmount(const std::string &str)
{
    if (str.empty())
        return false;
    return validateString(str, amountChars);
}

//////////////////////////////////////////////////////////////////////////////
//
// Argument definitions
//

// When adding new arguments to a category, please keep alphabetical ordering,
// where appropriate. Do not translate _(...) addDebugArg help text: there are
// many technical terms, and only a very small audience, so it would be an
// unnecessary stress to translators.

static void addHelpOptions(AllowedArgs& allowedArgs)
{
    allowedArgs
        .addHeader(_("Help options:"))
        .addArg("?,h,help", optionalBool, _("This help message"))
        .addArg("version", optionalBool, _("Print version and exit"))
        .addArg("help-debug", optionalBool, _("Show all debugging options (usage: --help -help-debug)"))
        ;
}

static void addChainSelectionOptions(AllowedArgs& allowedArgs)
{
    allowedArgs
        .addHeader(_("Chain selection options:"))
        .addArg("testnet", optionalBool, _("Use the test3 chain"))
        .addArg("testnet4", optionalBool, _("Use the test4 chain"))
        .addArg("scalenet", optionalBool, _("Use the scaling test chain"))
        .addDebugArg("regtest", optionalBool,
            "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
            "This is intended for regression testing tools and app development.")
        ;
}

static void addConfigurationLocationOptions(AllowedArgs& allowedArgs)
{
    allowedArgs
        .addHeader(_("Configuration location options:"))
        .addArg("conf=<file>", requiredStr, strprintf(_("Specify configuration file (default: %s)"), hubConfFilename()))
        .addArg("datadir=<dir>", requiredStr, _("Specify data directory"))
        ;
}

static void addGeneralOptions(AllowedArgs& allowedArgs, HelpMessageMode mode)
{
    allowedArgs
        .addHeader(_("General options:"))
        .addArg("alertnotify=<cmd>", requiredStr, _("Execute command when a relevant alert is received or we see a really long fork (%s in cmd is replaced by message)"))
        .addArg("blocknotify=<cmd>", requiredStr, _("Execute command when the best block changes (%s in cmd is replaced by block hash)"))
        .addDebugArg("blocksonly", optionalBool, strprintf(_("Whether to operate in a blocks only mode (default: %u)"), DefaultBlocksOnly))
        .addArg("checkblocks=<n>", requiredInt, strprintf(_("How many blocks to check at startup (default: %u, 0 = all)"), DefaultCheckBlocks))
        .addArg("checklevel=<n>", requiredInt, strprintf(_("How thorough the block verification of -checkblocks is (0-4, default: %u)"), DefaultCheckLevel))
        ;

#ifndef WIN32
    if (mode == HMM_HUB)
        allowedArgs.addArg("daemon", optionalBool, _("Run in the background as a daemon and accept commands"));
#endif

    allowedArgs
        .addArg("maxorphantx=<n>", requiredInt, strprintf(_("Keep at most <n> unconnectable transactions in memory (default: %u)"), DefaultMaxOrphanTransactions))
        .addArg("maxmempool=<n>", requiredInt, strprintf(_("Keep the transaction memory pool below <n> megabytes (default: %u)"), DefaultMaxMempoolSize))
        .addArg("mempoolexpiry=<n>", requiredInt, strprintf(_("Do not keep transactions in the mempool longer than <n> hours (default: %u)"), DefaultMempoolExpiry))
#ifndef WIN32
        .addArg("pid=<file>", requiredStr, strprintf(_("Specify pid file (default: %s)"), hubPidFilename()))
#endif
        .addArg("reindex", optionalBool, _("Rebuild block chain index from current blk000??.dat files on startup"))
        .addArg("blockdatadir=<dir>", requiredStr, "List a fallback directory to find blocks/blk* files")
        ;
}

static void addConnectionOptions(AllowedArgs& allowedArgs)
{
    allowedArgs
        .addHeader(_("Connection options:"))
        .addArg("addnode=<ip>", requiredStr, _("Add a node to connect to and attempt to keep the connection open"))
        .addArg("banscore=<n>", requiredInt, strprintf(_("Threshold for disconnecting misbehaving peers (default: %u)"), DefaultBanscoreThreshold))
        .addArg("bantime=<n>", requiredInt, strprintf(_("Number of seconds to keep misbehaving peers from reconnecting (default: %u)"), DefaultMisbehavingBantime))
        .addArg("bind=<addr>", requiredStr, _("Bind to given address and always listen on it. Use [host]:port notation for IPv6"))
        .addArg("connect=<ip>", optionalStr, _("Connect only to the specified node(s)"))
        .addArg("discover", optionalBool, _("Discover own IP addresses (default: true when listening and no -externalip or -proxy)"))
        .addArg("dns", optionalBool, _("Allow DNS lookups for -addnode, -seednode and -connect") + " " + strprintf(_("(default: %u)"), DefaultNameLookup))
        .addArg("dnsseed", optionalBool, _("Query for peer addresses via DNS lookup, if low on addresses (default: true unless -connect)"))
        .addArg("externalip=<ip>", requiredStr, _("Specify your own public address"))
        .addArg("forcednsseed", optionalBool, strprintf(_("Always query for peer addresses via DNS lookup (default: %u)"), DefaultForceDnsSeed))
        .addArg("listen", optionalBool, _("Accept connections from outside (default: true if no -proxy or -connect)"))
        .addArg("listenonion", optionalBool, strprintf(_("Automatically create Tor hidden service (default: %d)"), DefaultListenOnion))
        .addArg("maxconnections=<n>", optionalInt, strprintf(_("Maintain at most <n> connections to peers (default: %u)"), DefaultMaxPeerConnections))
        .addArg("min-thin-peers=<n>", requiredInt, strprintf(_("Maintain at minimum <n> connections to thin-capable peers (default: %d)"), DefaultMinThinPeers))
        .addArg("maxreceivebuffer=<n>", requiredInt, strprintf(_("Maximum per-connection receive buffer, <n>*1000 bytes (default: %u)"), DefaultMaxReceiveBuffer))
        .addArg("maxsendbuffer=<n>", requiredInt, strprintf(_("Maximum per-connection send buffer, <n>*1000 bytes (default: %u)"), DefaultMaxSendBuffer))
        .addArg("onion=<ip:port>", requiredStr, strprintf(_("Use separate SOCKS5 proxy to reach peers via Tor hidden services (default: %s)"), "-proxy"))
        .addArg("onlynet=<net>", requiredStr, _("Only connect to nodes in network <net> (ipv4, ipv6 or onion)"))
        .addArg("permitbaremultisig", optionalBool, strprintf(_("Relay non-P2SH multisig (default: %u)"), DefaultPermitBareMultisig))
        .addArg("peerbloomfilters", optionalBool, strprintf(_("Support filtering of blocks and transaction with bloom filters (default: %u)"), 1))
        .addDebugArg("enforcenodebloom", optionalBool, strprintf("Enforce minimum protocol version to limit use of bloom filters (default: %u)", 0))
        .addArg("port=<port>", requiredInt, strprintf(_("Listen for connections on <port> (default: %u, testnet: %u, testnet4: %u or scalenet: %u)"), DefaultMainnetPort, DefaultTestnetPort, DefaultTestnet4Port, DefaultScalenetPort))
        .addArg("proxy=<ip:port>", requiredStr, _("Connect through SOCKS5 proxy"))
        .addArg("proxyrandomize", optionalBool, strprintf(_("Randomize credentials for every proxy connection. This enables Tor stream isolation (default: %u)"), DefaultProxyRandomize))
        .addArg("seednode=<ip>", requiredStr, _("Connect to a node to retrieve peer addresses, and disconnect"))
        .addArg("timeout=<n>", requiredInt, strprintf(_("Specify connection timeout in milliseconds (minimum: 1, default: %d)"), DefaultConnectTimeout))
        .addArg("torcontrol=<ip>:<port>", requiredStr, strprintf(_("Tor control port to use if onion listening enabled (default: %s)"), DefaultTorControl))
        .addArg("torpassword=<pass>", requiredStr, _("Tor control port password (default: empty)"))
#ifdef USE_UPNP
#if USE_UPNP
        .addArg("upnp", optionalBool, _("Use UPnP to map the listening port (default: true when listening and no -proxy)"))
#else
        .addArg("upnp", optionalBool, _("Use UPnP to map the listening port (default: false)"))
#endif
#endif
        .addArg("whitebind=<addr>", requiredStr, _("Bind to given address and whitelist peers connecting to it. Use [host]:port notation for IPv6"))
        .addArg("whitelist=<netmask>", requiredStr, _("Whitelist peers connecting from the given netmask or IP address. Can be specified multiple times.") +
            " " + _("Whitelisted peers cannot be DoS banned and their transactions are always relayed, even if they are already in the mempool, useful e.g. for a gateway"))
        .addArg("whitelistrelay", optionalBool, strprintf(_("Accept relayed transactions received from whitelisted peers even when not relaying transactions (default: %d)"), DefaultWhitelistRelay))
        .addArg("whitelistforcerelay", optionalBool, strprintf(_("Force relay of transactions from whitelisted peers even they violate local relay policy (default: %d)"), DefaultWhitelistForceRelay))
        .addArg("maxuploadtarget=<n>", requiredInt, strprintf(_("Tries to keep outbound traffic under the given target (in MiB per 24h), 0 = no limit (default: %d)"), DefaultMaxUploadTarget))
        ;
}

static void addWalletOptions(AllowedArgs& allowedArgs)
{
#ifdef ENABLE_WALLET
    allowedArgs
        .addHeader(_("Wallet options:"))
        .addArg("disablewallet", optionalBool, _("Do not load the wallet and disable wallet RPC calls"))
        .addArg("keypool=<n>", requiredInt, strprintf(_("Set key pool size to <n> (default: %u)"), DefaultKeypoolSize))
        .addArg("fallbackfee=<amt>", requiredAmount, strprintf(_("A fee rate (in BCH/kB) that will be used when fee estimation has insufficient data (default: %s)"),
            FormatMoney(DefaultFallbackFee)))
        .addArg("mintxfee=<amt>", requiredAmount, strprintf(_("Fees (in BCH/kB) smaller than this are considered zero fee for transaction creation (default: %s)"),
                FormatMoney(DefaultTransactionMinfee)))
        .addArg("paytxfee=<amt>", requiredAmount, strprintf(_("Fee (in BCH/kB) to add to transactions you send (default: %s)"),
            FormatMoney(DefaultTransactionFee)))
        .addArg("rescan", optionalBool, _("Rescan the block chain for missing wallet transactions on startup"))
        .addArg("salvagewallet", optionalBool, _("Attempt to recover private keys from a corrupt wallet.dat on startup"))
        .addArg("sendfreetransactions", optionalBool, strprintf(_("Send transactions as zero-fee transactions if possible (default: %u)"), DefaultSendFreeTransactions))
        .addArg("spendzeroconfchange", optionalBool, strprintf(_("Spend unconfirmed change when sending transactions (default: %u)"), DefaultSpendZeroconfChange))
        .addArg("txconfirmtarget=<n>", requiredInt, strprintf(_("If paytxfee is not set, include enough fee so transactions begin confirmation on average within n blocks (default: %u)"), defaultTxConfirmTarget))
        .addArg("maxtxfee=<amt>", requiredAmount, strprintf(_("Maximum total fees (in BCH) to use in a single wallet transaction; setting this too low may abort large transactions (default: %s)"),
            FormatMoney(DefaultTransactionMaxFee)))
        .addArg("wallet=<file>", requiredStr, _("Specify wallet file (within data directory)") + " " + strprintf(_("(default: %s)"), "wallet.dat"))
        .addArg("walletbroadcast", optionalBool, _("Make the wallet broadcast transactions") + " " + strprintf(_("(default: %u)"), DefaultWalletBroadcast))
        .addArg("walletnotify=<cmd>", requiredStr, _("Execute command when a wallet transaction changes (%s in cmd is replaced by TxID)"))
        .addArg("zapwallettxes=<mode>", optionalInt, _("Delete all wallet transactions and only recover those parts of the blockchain through -rescan on startup") +
            " " + _("(1 = keep tx meta data e.g. account owner and payment request information, 2 = drop tx meta data)"))
        ;
#endif
}

static void addZmqOptions(AllowedArgs& allowedArgs)
{
#if ENABLE_ZMQ
    allowedArgs
        .addHeader(_("ZeroMQ notification options:"))
        .addArg("zmqpubhashblock=<address>", requiredStr, _("Enable publish hash block in <address>"))
        .addArg("zmqpubhashtx=<address>", requiredStr, _("Enable publish hash transaction in <address>"))
        .addArg("zmqpubrawblock=<address>", requiredStr, _("Enable publish raw block in <address>"))
        .addArg("zmqpubrawtx=<address>", requiredStr, _("Enable publish raw transaction in <address>"))
        ;
#endif
}

static void addDebuggingOptions(AllowedArgs& allowedArgs, HelpMessageMode)
{
    allowedArgs
        .addHeader(_("Debugging/Testing options:"))
        .addArg("uacomment=<cmt>", requiredStr, _("Append comment to the user agent string"))
        .addDebugArg("checkblockindex", optionalBool, strprintf("Do a full consistency check for mapBlockIndex, setBlockIndexCandidates, chainActive and mapBlocksUnlinked occasionally (default: %u)", false))
        .addDebugArg("checkpoints", optionalBool, strprintf("Disable expensive verification for known chain history (default: %u)", DefaultCheckpointsEnabled))
#ifdef ENABLE_WALLET
        .addDebugArg("dblogsize=<n>", requiredInt, strprintf("Flush wallet database activity from memory to disk log every <n> megabytes (default: %u)", DefaultWalletDBLogSize))
#endif
        .addDebugArg("disablesafemode", optionalBool, strprintf("Disable safemode, override a real safe mode event (default: %u)", DefaultDisableSafemode))
        .addDebugArg("testsafemode", optionalBool, strprintf("Force safe mode (default: %u)", DefaultTestSafeMode))
        .addDebugArg("dropmessagestest=<n>", requiredInt, "Randomly drop 1 of every <n> network messages")
        .addDebugArg("fuzzmessagestest=<n>", requiredInt, "Randomly fuzz 1 of every <n> network messages")
#ifdef ENABLE_WALLET
        .addDebugArg("flushwallet", optionalBool, strprintf("Run a thread to flush wallet periodically (default: %u)", DefaultFlushWallet))
#endif
        .addDebugArg("stopafterblockimport", optionalBool, strprintf("Stop running after importing blocks from disk (default: %u)", DefaultStopAfterBlockImport))
        .addDebugArg("limitancestorcount=<n>", requiredInt, strprintf("Do not accept transactions if number of in-mempool ancestors is <n> or more (default: %u)", DefaultAncestorLimit))
        .addDebugArg("limitancestorsize=<n>", requiredInt, strprintf("Do not accept transactions whose size with all in-mempool ancestors exceeds <n> kilobytes (default: %u)", DefaultAncestorSizeLimit))
        .addDebugArg("limitdescendantcount=<n>", requiredInt, strprintf("Do not accept transactions if any ancestor would have <n> or more in-mempool descendants (default: %u)", DefaultDescendantLimit))
        .addDebugArg("limitdescendantsize=<n>", requiredInt, strprintf("Do not accept transactions if any ancestor would have more than <n> kilobytes of in-mempool descendants (default: %u).", DefaultDescendantSizeLimit))
        .addArg("gen", optionalBool,  strprintf(_("Generate coins (default: %u)"), DefaultGenerateCoins))
        .addArg("genproclimit=<n>", requiredInt, strprintf(_("Set the number of threads for coin generation if enabled (-1 = all cores, default: %d)"), DefaultGenerateThreads))
        .addArg("gencoinbase=<pubkey>", requiredStr, "When generating coins a coinbase has to be provided in the form of a public key")
        .addArg("logips", optionalBool, strprintf(_("Include IP addresses in debug output (default: %u)"), DEFAULT_LOGIPS))
        .addDebugArg("mocktime=<n>", requiredInt, "Replace actual time with <n> seconds since epoch (default: 0)")
        .addDebugArg("limitfreerelay=<n>", optionalInt, strprintf("Continuously rate-limit free transactions to <n>*1000 bytes per minute (default: %u)", DefaultLimitFreeRelay))
        .addDebugArg("relaypriority", optionalBool, strprintf("Require high priority for relaying free or low-fee transactions (default: %u)", DefaultRelayPriority))
        .addDebugArg("maxsigcachesize=<n>", requiredInt, strprintf("Limit size of signature cache to <n> MiB (default: %u)", DefaultMaxSigCacheSize))
        .addArg("printtoconsole", optionalBool, _("Send trace/debug info to console as well as to hub.log file"))
        .addDebugArg("printpriority", optionalBool, strprintf("Log transaction priority and fee per kB when mining blocks (default: %u)", DefaultGeneratePriorityLogging))
#ifdef ENABLE_WALLET
        .addDebugArg("privdb", optionalBool, strprintf("Sets the DB_PRIVATE flag in the wallet db environment (default: %u)", DefaultWalletPrivDb))
#endif
        .addArg("shrinkdebugfile", optionalBool, _("Shrink hub.log file on client startup (default: true when no -debug)"))
        .addDebugArg("catch-crash", optionalBool, "Enable the crash-catcher which creates a backtrace file on segfault")
        ;
}

static void addNodeRelayOptions(AllowedArgs& allowedArgs)
{
    allowedArgs
        .addHeader(_("Node relay options:"))
        .addDebugArg("acceptnonstdtxn", optionalBool, strprintf("Relay and mine \"non-standard\" transactions (%sdefault: %u)", "testnet/regtest only; ", true))
        .addArg("blocksizeacceptlimit=<n>", requiredAmount, strprintf("This node will not accept blocks larger than this limit. Unit is in MB (default: %.1f)", DefaultBlockAcceptSize / 1e6))
        .addDebugArg("blocksizeacceptlimitbytes,excessiveblocksize=<n>", requiredInt, strprintf("This node will not accept blocks larger than this limit. Unit is in bytes. Superseded by -blocksizeacceptlimit (default: %u)", DefaultBlockAcceptSize))
        .addArg("datacarrier", optionalBool, strprintf(_("Relay and mine data carrier transactions (default: %u)"), DefaultAcceptDataCarrier))
        .addArg("datacarriersize=<n>", requiredInt, strprintf(_("Maximum size of data in data carrier transactions we relay and mine (default: %u)"), MaxOpReturnRelay))
        .addArg("expeditedblock=<host>", requiredStr, _("Request expedited blocks from this host whenever we are connected to it"))
        .addArg("maxexpeditedblockrecipients=<n>", requiredInt, _("The maximum number of nodes this node will forward expedited blocks to"))
        .addArg("maxexpeditedtxrecipients=<n>", requiredInt, _("The maximum number of nodes this node will forward expedited transactions to"))
        .addArg("minrelaytxfee=<amt>", requiredAmount, strprintf(_("Fees (in BCH/kB) smaller than this are considered zero fee for relaying, mining and transaction creation (default: %s)"),
            FormatMoney(DefaultMinRelayTxFee)))
        .addArg("use-thinblocks", optionalBool, _("Enable thin blocks to speed up the relay of blocks (default: false)"))
        ;
}

static void addBlockCreationOptions(AllowedArgs& allowedArgs)
{
    allowedArgs
        .addHeader(_("Block creation options:"))
        .addArg("blockminsize=<n>", requiredInt, strprintf(_("Set minimum block size in bytes (default: %u)"), DefaultBlockMinSize))
        .addArg("blockmaxsize=<n>", requiredInt, strprintf("Set maximum block size in bytes (default: %d)", DefaultBlockMAxSize))
        .addArg("blockprioritysize=<n>", requiredInt, strprintf(_("Set maximum size of high-priority/low-fee transactions in bytes (default: %d)"), DefaultBlockPrioritySize))
        .addDebugArg("blockversion=<n>", requiredInt, "Override block version to test forking scenarios")
        ;
}

static void addRpcServerOptions(AllowedArgs& allowedArgs)
{
    allowedArgs
        .addHeader(_("RPC server options:"))
        .addArg("server", optionalBool, _("Accept command line and JSON-RPC commands"))
        .addArg("rest", optionalBool, strprintf(_("Accept public REST requests (default: %u)"), DefaultRestEnable))
        .addArg("rpcbind=<addr>", requiredStr, _("Bind to given address to listen for JSON-RPC connections. Use [host]:port notation for IPv6. This option can be specified multiple times (default: bind to all interfaces)"))
        .addArg("rpccookiefile=<loc>", requiredStr, _("Location of the auth cookie (default: data dir)"))
        .addArg("rpcuser=<user>", requiredStr, _("Username for JSON-RPC connections"))
        .addArg("rpcpassword=<pw>", requiredStr, _("Password for JSON-RPC connections"))
        .addArg("rpcauth=<userpw>", requiredStr, _("Username and hashed password for JSON-RPC connections. The field <userpw> comes in the format: <USERNAME>:<SALT>$<HASH>. A canonical python script is included in share/rpcuser. This option can be specified multiple times"))
        .addArg("rpcport=<port>", requiredInt, strprintf(_("Listen for JSON-RPC connections on <port> (default: %u, testnet: %u, testnet4: %u or scalenet: %u)"), BaseParams(CBaseChainParams::MAIN).RPCPort(), BaseParams(CBaseChainParams::TESTNET).RPCPort(), BaseParams(CBaseChainParams::TESTNET4).RPCPort(), BaseParams(CBaseChainParams::SCALENET).RPCPort()))
        .addArg("rpcallowip=<ip>", requiredStr, _("Allow JSON-RPC connections from specified source. Valid for <ip> are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24). This option can be specified multiple times"))
        .addArg("rpcthreads=<n>", requiredInt, strprintf(_("Set the number of threads to service RPC calls (default: %d)"), DefaultHttpThreads))
        .addDebugArg("rpcworkqueue=<n>", requiredInt, strprintf("Set the depth of the work queue to service RPC calls (default: %d)", DefaultHttpWorkQueue))
        .addDebugArg("rpcservertimeout=<n>", requiredInt, strprintf("Timeout during HTTP requests (default: %d)", DefaultHttpServerTimeout))
        ;
}

static void addApiServerOptions(AllowedArgs& allowedArgs)
{
    allowedArgs
        .addHeader("Api server options:")
        .addArg("api", optionalBool, _("Accept API connections (default true)"))
        .addArg("api_connection_per_ip", requiredInt, "Maximum amount of connections from a certain IP")
        .addArg("api_disallow_v6", optionalBool, "Do not allow incoming ipV6 connections")
        .addArg("api_max_addresses", requiredInt, "Maximum amount of addresses a connection can listen on")
        .addArg("apilisten=<addr>", requiredStr, strprintf("Bind to given address to listen for api server connections. Use [host]:port notation for IPv6. This option can be specified multiple times (default 127.0.0.1:%s and [::1]:%s)", BaseParams(CBaseChainParams::MAIN).ApiServerPort(), BaseParams(CBaseChainParams::MAIN).ApiServerPort()));
}

static void addUiOptions(AllowedArgs& allowedArgs)
{
    allowedArgs
        .addHeader(_("UI Options:"))
        .addDebugArg("allowselfsignedrootcertificates", optionalBool, strprintf("Allow self signed root certificates (default: %u)", DefaultSelfsignedRootcerts))
        .addArg("choosedatadir", optionalBool, strprintf(_("Choose data directory on startup (default: %u)"), DefaultChooseDatadir))
        .addArg("lang=<lang>", requiredStr, _("Set language, for example \"de_DE\" (default: system locale)"))
        .addArg("min", optionalBool, _("Start minimized"))
        .addArg("rootcertificates=<file>", optionalStr, _("Set SSL root certificates for payment request (default: -system-)"))
        .addArg("splash", optionalBool, strprintf(_("Show splash screen on startup (default: %u)"), DefaultSplashscreen))
        .addArg("resetguisettings", optionalBool, _("Reset all settings changes made over the GUI"))
        .addDebugArg("uiplatform=<platform>", requiredStr, strprintf("Select platform to customize UI for (one of windows, macosx, other; default: %s)", DefaultUIPlatform))
        ;
}

static void addAllNodeOptions(AllowedArgs& allowedArgs, HelpMessageMode mode)
{
    addHelpOptions(allowedArgs);
    addConfigurationLocationOptions(allowedArgs);
    addGeneralOptions(allowedArgs, mode);
    addConnectionOptions(allowedArgs);
    addWalletOptions(allowedArgs);
    addZmqOptions(allowedArgs);
    addDebuggingOptions(allowedArgs, mode);
    addChainSelectionOptions(allowedArgs);
    addNodeRelayOptions(allowedArgs);
    addBlockCreationOptions(allowedArgs);
    addRpcServerOptions(allowedArgs);
    addApiServerOptions(allowedArgs);
    if (mode == HMM_HUB_QT)
        addUiOptions(allowedArgs);
}

HubCli::HubCli()
{
    addHelpOptions(*this);
    addChainSelectionOptions(*this);
    addConfigurationLocationOptions(*this);

    addHeader(_("RPC client options:"))
        .addArg("rpcconnect=<ip>", requiredStr, strprintf(_("Send commands to node running on <ip> (default: %s)"), DEFAULT_RPCCONNECT))
        .addArg("rpcport=<port>", requiredInt, strprintf(_("Connect to JSON-RPC on <port> (default: %u, testnet: %u, testnet4: %u or scalenet: %u)"), BaseParams(CBaseChainParams::MAIN).RPCPort(), BaseParams(CBaseChainParams::TESTNET).RPCPort(), BaseParams(CBaseChainParams::TESTNET4).RPCPort(), BaseParams(CBaseChainParams::SCALENET).RPCPort()))
        .addArg("rpcwait", optionalBool, _("Wait for RPC server to start"))
        .addArg("rpcuser=<user>", requiredStr, _("Username for JSON-RPC connections"))
        .addArg("rpcpassword=<pw>", requiredStr, _("Password for JSON-RPC connections"))
        .addArg("rpcclienttimeout=<n>", requiredInt, strprintf(_("Timeout during HTTP requests (default: %d)"), DEFAULT_HTTP_CLIENT_TIMEOUT))
        ;
}

Hub::Hub()
{
    addAllNodeOptions(*this, HMM_HUB);
}

HubQt::HubQt()
{
    addAllNodeOptions(*this, HMM_HUB_QT);
}

BitcoinTx::BitcoinTx()
{
    addHelpOptions(*this);
    addChainSelectionOptions(*this);

    addHeader(_("Transaction options:"))
        .addArg("create", optionalBool, _("Create new, empty TX."))
        .addArg("json", optionalBool, _("Select JSON output"))
        .addArg("txid", optionalBool, _("Output only the hex-encoded transaction id of the resultant transaction."))
        .addDebugArg("", optionalBool, "Read hex-encoded bitcoin transaction from stdin.")
        ;
}

ConfigFile::ConfigFile()
{
    // Merges all allowed args from hub-cli, hub, and hub-qt.
    // Excludes args from BitcoinTx, because bitcoin-tx does not read
    // from the config file. Does not set a help message, because the
    // program does not output a config file help message anywhere.

    HubCli hubCli;
    Hub hub;
    HubQt hubQt;

    m_args.insert(hubCli.getArgs().begin(), hubCli.getArgs().end());
    m_args.insert(hub.getArgs().begin(), hub.getArgs().end());
    m_args.insert(hubQt.getArgs().begin(), hubQt.getArgs().end());
}

}
