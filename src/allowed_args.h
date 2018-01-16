/*
 * This file is part of the Flowee project
 * Copyright (C) 2017 Stephen McCarthy
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

#ifndef FLOWEE_ALLOWED_ARGS_H
#define FLOWEE_ALLOWED_ARGS_H

#include <functional>
#include <list>
#include <map>
#include <string>

namespace AllowedArgs {

/**
 * Format a string to be used as group of options in help messages.
 *
 * @param message Group name (e.g. "RPC server options:").
 * @return The formatted string.
 */
std::string HelpMessageGroup(const std::string& message);

/**
 * Format a string to be used as option description in help messages.
 *
 * @param option Option message (e.g. "-rpcuser=<user>").
 * @param message Option description (e.g. "Username for JSON-RPC connections").
 * @return The formatted string.
 */
std::string HelpMessageOpt(const std::string& option, const std::string& message);

struct HelpComponent
{
    std::string text;
    bool debug;
};

typedef std::function<bool(const std::string& str)> CheckValueFunc;

/**
 * Provides functionality for validating program arguments and argument values.
 * Additionally provides functionality for creating help text for all allowed
 * arguments for a program.
 */
class AllowedArgs
{
protected:
    std::map<std::string, CheckValueFunc> m_args;
    std::list<HelpComponent> m_helpList;

public:

    /**
     * Add header text for help message.
     *
     * @param strHeader Header text (e.g. "General options:").
     * @param helpDebug If true, the header will only be shown on -help-debug.
     * @return This instance.
     */
    AllowedArgs& addHeader(const std::string& strHeader, bool helpDebug = false);

    /**
     * Add new allowed argument(s).
     *
     * @param strArgs The argument name(s). Multiple arg names must be
     *     separated by a ',' character. An example value can be provided
     *     after an '=' character. If multiple args are provided only the
     *     first will be shown in help text, except on -help-debug.
     *     Examples: "?,help,h", "pid=<file>".
     * @param checkValueFunc Verification function for the argument value.
     * @param strHelp The help text.
     * @param helpDebug If true, the help text for this arg will only be shown
     *     on -help-debug.
     * @return This instance.
     */
    AllowedArgs& addArg(const std::string& strArgs, CheckValueFunc checkValueFunc, const std::string& strHelp, bool helpDebug = false);

    /**
     * Add new allowed argument(s). Calls addArg with helpDebug set to true.
     *
     * @see addArg()
     */
    AllowedArgs& addDebugArg(const std::string& strArgsDefinition, CheckValueFunc checkValueFunc, const std::string& strHelp);

    /**
     * @return The map of argument names to CheckValueFuncs.
     */
    const std::map<std::string, CheckValueFunc>& getArgs() const { return m_args; }

    /**
     * Checks if a given argument name/value pair is valid. The pair is valid
     * if the argument name has been added via an addArg call, and the
     * associated checkValueFunc returns true when called with the value. If
     * the pair is not valid, a std::runtime_error is thrown.
     *
     * @param strArg The argument name.
     * @param strValue The argument value.
     */
    void checkArg(const std::string& strArg, const std::string& strValue) const;

    /**
     * @return Help message for all arguments that have been added.
     *     If -help-debug is specified, then debug help is included.
     */
    std::string helpMessage() const;
};

class BitcoinCli : public AllowedArgs
{
public:
    BitcoinCli();
};

class Bitcoind : public AllowedArgs
{
public:
    Bitcoind();
};

class BitcoinQt : public AllowedArgs
{
public:
    BitcoinQt();
};

class BitcoinTx : public AllowedArgs
{
public:
    BitcoinTx();
};

class ConfigFile: public AllowedArgs
{
public:
    ConfigFile();
};

} // namespace AllowedArgs

#endif
