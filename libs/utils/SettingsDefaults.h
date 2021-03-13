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

#ifndef SETTINGS_DEFAULTS_H
#define SETTINGS_DEFAULTS_H

#include "amount.h"
#include <string>

namespace Settings {

// /////// Validation
constexpr int DefaultCheckBlocks = 5;
constexpr uint32_t DefaultCheckLevel = 3;

// /////// NET

/** Default for -blocksizeacceptlimit */
constexpr int32_t DefaultBlockAcceptSize = 128000000;

constexpr bool DefaultAcceptDataCarrier = true;
constexpr int MaxOpReturnRelay = 220; //! bytes
constexpr bool DefaultRelayPriority = true;

/** Default for setting that we download and accept blocks only (no transactions, no mempool) */
constexpr bool DefaultBlocksOnly = false;
constexpr uint32_t DefaultBanscoreThreshold = 100;

// NOTE: When adjusting this, update rpcnet:setban's help ("24h")
constexpr uint32_t DefaultMisbehavingBantime = 60 * 60 * 24;  // Default 24-hour ban

//! -dns default
constexpr int DefaultNameLookup = true;

/// download peers from DNS
constexpr bool DefaultForceDnsSeed = false;

constexpr int DefaultHttpThreads=4;
constexpr int DefaultHttpWorkQueue=16;
constexpr int DefaultHttpServerTimeout=30;

/// Tor
constexpr bool DefaultListenOnion = false;
static const std::string DefaultTorControl = "127.0.0.1:9051";

/** The default for -maxuploadtarget. 0 = Unlimited */
constexpr uint64_t DefaultMaxUploadTarget = 0;

/** The maximum number of peer connections to maintain. */
constexpr uint32_t DefaultMaxPeerConnections = 125;

/** The default minimum number of thin nodes to connect to */
constexpr int DefaultMinThinPeers = 0;

constexpr uint32_t DefaultMaxReceiveBuffer = 5 * 1000;
constexpr uint32_t DefaultMaxSendBuffer    = 1 * 1000;

constexpr int DefaultMainnetPort = 8333;
constexpr int DefaultTestnetPort = 18333;
constexpr int DefaultTestnet4Port = 28333;
constexpr int DefaultScalenetPort = 38333;

constexpr bool DefaultProxyRandomize = true;

//! -timeout default
constexpr int DefaultConnectTimeout = 5000;

constexpr bool DefaultWhitelistRelay = true;
constexpr bool DefaultWhitelistForceRelay = true;

// /////// Mempool

/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
constexpr uint32_t DefaultMaxOrphanTransactions = 5000;

/** Default for -maxmempool, maximum megabytes of mempool memory usage */
constexpr uint32_t DefaultMaxMempoolSize = 300;

/** Default for -mempoolexpiry, expiration time for mempool transactions in hours */
constexpr uint32_t DefaultMempoolExpiry = 5;

/** Default for -permitbaremultisig */
constexpr bool DefaultPermitBareMultisig = true;

/** Default for -limitancestorcount, max number of in-mempool ancestors */
constexpr uint32_t DefaultAncestorLimit = 5000;
/** Default for -limitancestorsize, maximum kilobytes of tx + all in-mempool ancestors */
constexpr uint32_t DefaultAncestorSizeLimit = 1000;
/** Default for -limitdescendantcount, max number of in-mempool descendants */
constexpr uint32_t DefaultDescendantLimit = 5000;
/** Default for -limitdescendantsize, maximum kilobytes of in-mempool descendants */
constexpr uint32_t DefaultDescendantSizeLimit = 1000;

constexpr uint32_t DefaultLimitFreeRelay = 15;

// ///////// Wallet

constexpr uint32_t DefaultKeypoolSize = 100;
//! -fallbackfee default
constexpr int64_t DefaultFallbackFee = 20000;

//! -mintxfee default
constexpr int64_t DefaultTransactionMinfee = 1000;

//! -maxtxfee default
constexpr int64_t DefaultTransactionMaxFee= 0.1 * COIN;


//! -paytxfee default
constexpr int64_t DefaultTransactionFee = 0;

//! Default for -sendfreetransactions
constexpr bool DefaultSendFreeTransactions = false;

//! Default for -spendzeroconfchange
constexpr bool DefaultSpendZeroconfChange = true;

//! -txconfirmtarget default
constexpr uint32_t defaultTxConfirmTarget = 2;

constexpr bool DefaultWalletBroadcast = true;

constexpr uint32_t DefaultWalletDBLogSize = 100;

constexpr bool DefaultFlushWallet = true;

constexpr bool DefaultWalletPrivDb = true;

// //// config (soo meta)

inline const char * hubPidFilename() {
    return "floweethehub.pid";
}

inline const char * hubConfFilename() {
    return "flowee.conf";
}

// //// Mining

/** Default for -blockmaxsize and -blockminsize, which control the range of sizes the mining code will create **/
constexpr uint32_t DefaultBlockMAxSize = 8000000;
constexpr uint32_t DefaultBlockMinSize = 0;
/** Default for -blockprioritysize, maximum space for zero/low-fee transactions **/
constexpr uint32_t DefaultBlockPrioritySize = 100000;

constexpr bool DefaultGenerateCoins = false;
constexpr int DefaultGenerateThreads = 1;
constexpr bool DefaultGeneratePriorityLogging = false;


// /////// Qt GUI
constexpr bool DefaultChooseDatadir = false;
constexpr bool DefaultSelfsignedRootcerts = false;
constexpr bool DefaultSplashscreen = true;

static const std::string DefaultUIPlatform =
#if defined(Q_OS_MAC)
    "macosx"
#elif defined(Q_OS_WIN)
    "windows"
#else
    "other"
#endif
    ;

// DoS prevention: limit cache size to 32MB (over 1000000 entries on 64-bit
// entries on 64-bit systems).
// systems). Due to how we count cache size, actual memory usage is slightly
// more (~32.25 MB)
constexpr uint32_t DefaultMaxSigCacheSize = 32;

constexpr bool DefaultRestEnable = false;
constexpr bool DefaultDisableSafemode = false;
constexpr bool DefaultStopAfterBlockImport = false;

constexpr bool DefaultCheckpointsEnabled = true;

constexpr bool DefaultTestSafeMode = false;

/** Default for -minrelaytxfee, minimum relay fee for transactions */
constexpr uint32_t DefaultMinRelayTxFee = 1000;

}
#endif
