/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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
static const signed int DefaultCheckBlocks = 5;
static const unsigned int DefaultCheckLevel = 3;

// /////// NET

/** Default for -blocksizeacceptlimit */
static const int32_t DefaultBlockAcceptSize = 128000000;

static const bool DefaultAcceptDataCarrier = true;
static const unsigned int MaxOpReturnRelay = 223; //! bytes (+1 for OP_RETURN, +2 for the pushdata opcodes)
static const bool DefaultRelayPriority = true;

/** Default for setting that we download and accept blocks only (no transactions, no mempool) */
static const bool DefaultBlocksOnly = false;
static const unsigned int DefaultBanscoreThreshold = 100;

// NOTE: When adjusting this, update rpcnet:setban's help ("24h")
static const unsigned int DefaultMisbehavingBantime = 60 * 60 * 24;  // Default 24-hour ban

//! -dns default
static const int DefaultNameLookup = true;

/// download peers from DNS
static const bool DefaultForceDnsSeed = false;

static const int DefaultHttpThreads=4;
static const int DefaultHttpWorkQueue=16;
static const int DefaultHttpServerTimeout=30;

/// Tor
static const bool DefaultListenOnion = false;
static const std::string DefaultTorControl = "127.0.0.1:9051";

/** The default for -maxuploadtarget. 0 = Unlimited */
static const uint64_t DefaultMaxUploadTarget = 0;

/** The maximum number of peer connections to maintain. */
static const unsigned int DefaultMaxPeerConnections = 125;

/** The default minimum number of thin nodes to connect to */
static const int DefaultMinThinPeers = 2;

static const unsigned int DefaultMaxReceiveBuffer = 5 * 1000;
static const unsigned int DefaultMaxSendBuffer    = 1 * 1000;

static const int DefaultMainnetPort = 8333;
static const int DefaultTestnetPort = 18333;

static const bool DefaultProxyRandomize = true;

//! -timeout default
static const int DefaultConnectTimeout = 5000;

static const bool DefaultWhitelistRelay = true;
static const bool DefaultWhitelistForceRelay = true;

// /////// Mempool

/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
static const unsigned int DefaultMaxOrphanTransactions = 5000;

/** Default for -maxmempool, maximum megabytes of mempool memory usage */
static const unsigned int DefaultMaxMempoolSize = 300;

/** Default for -mempoolexpiry, expiration time for mempool transactions in hours */
static const unsigned int DefaultMempoolExpiry = 72;

/** Default for -permitbaremultisig */
static const bool DefaultPermitBareMultisig = true;

/** Default for -limitancestorcount, max number of in-mempool ancestors */
static const unsigned int DefaultAncestorLimit = 50;
/** Default for -limitancestorsize, maximum kilobytes of tx + all in-mempool ancestors */
static const unsigned int DefaultAncestorSizeLimit = 101;
/** Default for -limitdescendantcount, max number of in-mempool descendants */
static const unsigned int DefaultDescendantLimit = 50;
/** Default for -limitdescendantsize, maximum kilobytes of in-mempool descendants */
static const unsigned int DefaultDescendantSizeLimit = 101;

static const unsigned int DefaultLimitFreeRelay = 15;

static const unsigned int DefaultBytesPerSigop = 20;

// ///////// Wallet

static const unsigned int DefaultKeypoolSize = 100;
//! -fallbackfee default
static const CAmount DefaultFallbackFee = 20000;

//! -mintxfee default
static const CAmount DefaultTransactionMinfee = 1000;

//! -maxtxfee default
static const CAmount DefaultTransactionMaxFee= 0.1 * COIN;


//! -paytxfee default
static const CAmount DefaultTransactionFee = 0;

//! Default for -sendfreetransactions
static const bool DefaultSendFreeTransactions = false;

//! Default for -spendzeroconfchange
static const bool DefaultSpendZeroconfChange = true;

//! -txconfirmtarget default
static const unsigned int defaultTxConfirmTarget = 2;

static const bool DefaultWalletBroadcast = true;

static const unsigned int DefaultWalletDBLogSize = 100;

static const bool DefaultFlushWallet = true;

static const bool DefaultWalletPrivDb = true;

// //// config (soo meta)

inline const char * hubPidFilename() {
    return "floweethehub.pid";
}

inline const char * hubConfFilename() {
    return "flowee.conf";
}

// //// Mining

/** Default for -blockmaxsize and -blockminsize, which control the range of sizes the mining code will create **/
static const unsigned int DefaultBlockMAxSize = 8000000;
static const unsigned int DefaultBlockMinSize = 0;
/** Default for -blockprioritysize, maximum space for zero/low-fee transactions **/
static const unsigned int DefaultBlockPrioritySize = 100000;

static const bool DefaultGenerateCoins = false;
static const int DefaultGenerateThreads = 1;
static const bool DefaultGeneratePriorityLogging = false;


// /////// Qt GUI
static const bool DefaultChooseDatadir = false;
static const bool DefaultSelfsignedRootcerts = false;
static const bool DefaultSplashscreen = true;

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
static const unsigned int DefaultMaxSigCacheSize = 32;

static const bool DefaultRestEnable = false;
static const bool DefaultDisableSafemode = false;
static const bool DefaultStopAfterBlockImport = false;

static const bool DefaultCheckpointsEnabled = true;

static const bool DefaultTestSafeMode = false;

/** Default for -minrelaytxfee, minimum relay fee for transactions */
static const unsigned int DefaultMinRelayTxFee = 1000;

}
#endif
