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

#ifndef FLOWEE_SERVERUTIL_H
#define FLOWEE_SERVERUTIL_H

#include <Logger.h>
#include <util.h>

#include <boost/filesystem/path.hpp>

extern bool fServer;
extern std::string strMiscWarning;
extern bool fLogIPs;

void SetupEnvironment();
bool SetupNetworking();

/**
 * .. and a wrapper that just calls func once
 */
template <typename Callable> void TraceThread(const char* name,  Callable func)
{
    std::string s = strprintf("bitcoin-%s", name);
    RenameThread(s.c_str());
    try {
        logDebug() << name << "thread start";
        func();
        logDebug() << name << "thread exit";
    }
    catch (const boost::thread_interrupted&) {
        logDebug() << name << "thread interrupt";
        throw;
    }
    catch (const std::exception &e) {
        logWarning() << name << e;
        throw;
    }
    catch (...) {
        logWarning() << "Exception received" << name;
        throw;
    }
}

void runCommand(const std::string &strCommand);

#endif
