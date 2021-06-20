/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2015 The Bitcoin Core developers
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

/**
 * Server/client environment: argument handling, config file parsing,
 * logging, thread wrappers
 */
#ifndef FLOWEE_UTIL_H
#define FLOWEE_UTIL_H

#if defined(HAVE_CONFIG_H)
#include "config/flowee-config.h"
#endif

#include "allowed_args.h"
#include "compat.h"
#include "tinyformat.h"
#include "utiltime.h"
#include "Logger.h"

#include <exception>
#include <map>
#include <vector>
#include <functional>
#include <mutex>

#include <boost/filesystem/path.hpp>
#include <boost/signals2/signal.hpp>
#include <boost/thread/exceptions.hpp>

// For bitcoin-cli
static const char DEFAULT_RPCCONNECT[] = "127.0.0.1";
static const int DEFAULT_HTTP_CLIENT_TIMEOUT = 900;
static const bool DEFAULT_LOGIPS        = false;

extern std::map<std::string, std::string> mapArgs;
extern std::map<std::string, std::vector<std::string> > mapMultiArgs;

/**
 * old deprecated method to do translation of GUI output.
 */
inline std::string _(const char* psz)
{
    return std::string(psz);
}

#define LogPrintf(...) Log::MessageLogger(BCH_MESSAGELOG_FILE, BCH_MESSAGELOG_LINE, BCH_MESSAGELOG_FUNC).infoCompat(nullptr, __VA_ARGS__)

/**
 * When we switch to C++11, this can be switched to variadic templates instead
 * of this macro-based construction (see tinyformat.h).
 */
#define MAKE_ERROR_AND_LOG_FUNC(n)                                        \
    /**   Print to hub.log if -debug=category switch is given OR category is NULL. */ \
    template<TINYFORMAT_ARGTYPES(n)> \
    static inline void LogPrint(const char* category, const char* format, TINYFORMAT_VARARGS(n)) { \
        Log::MessageLogger(nullptr, 0, nullptr).infoCompat(category, format, TINYFORMAT_PASSARGS(n)); \
    } \
    template<TINYFORMAT_ARGTYPES(n)> \
    static inline bool error(const char* format, TINYFORMAT_VARARGS(n)) { \
        Log::MessageLogger(nullptr, 0, nullptr).warning(format, TINYFORMAT_PASSARGS(n)); \
        return false;\
    }

TINYFORMAT_FOREACH_ARGNUM(MAKE_ERROR_AND_LOG_FUNC)

/**
 * Zero-arg versions of logging and error, these are not covered by
 * TINYFORMAT_FOREACH_ARGNUM
 */
static inline void LogPrint(const char* category, const char* format)
{
    Log::MessageLogger(nullptr, 0, nullptr).infoCompat(category, format);
}

static inline bool error(const char* format)
{
    Log::MessageLogger(nullptr, 0, nullptr).warning() << "ERROR:" << format;
    return false;
}

void PrintExceptionContinue(const std::exception *pex, const char* pszThread);
void ParseParameters(int argc, const char*const argv[], const Settings::AllowedArgs& allowedArgs);
bool TryCreateDirectory(const boost::filesystem::path& p);
boost::filesystem::path GetDefaultDataDir();
const boost::filesystem::path &GetDataDir(bool fNetSpecific = true);
void ClearDatadirCache();
boost::filesystem::path GetConfigFile(const std::string &filename = "");
void ReadConfigFile(std::map<std::string, std::string>& mapSettingsRet, std::map<std::string, std::vector<std::string> >& mapMultiSettingsRet);
#ifdef WIN32
boost::filesystem::path GetSpecialFolderPath(int nFolder, bool fCreate = true);
#endif

inline bool IsSwitchChar(char c)
{
#ifdef WIN32
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}

/**
 * Return string argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. "1")
 * @return command-line argument or default value
 */
std::string GetArg(const std::string& strArg, const std::string& strDefault);

/**
 * Return integer argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. 1)
 * @return command-line argument (0 if invalid number) or default value
 */
int64_t GetArg(const std::string& strArg, int64_t nDefault);

/**
 * Return boolean argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (true or false)
 * @return command-line argument or default value
 */
bool GetBoolArg(const std::string& strArg, bool fDefault);

/**
 * Set an argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param strValue Value (e.g. "1")
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetArg(const std::string& strArg, const std::string& strValue);

/**
 * Set a boolean argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param fValue Value (e.g. false)
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetBoolArg(const std::string& strArg, bool fValue);

void SetThreadPriority(int nPriority);
void RenameThread(const char* name);

/**
 * Looks for -regtest, -testnet, -testnet4, -scalenet and returns the appropriate BIP70 chain name.
 * @return CBaseChainParams::MAX_NETWORK_TYPES if an invalid combination is given. CBaseChainParams::MAIN by default.
 */
std::string ChainNameFromCommandLine();

#endif
