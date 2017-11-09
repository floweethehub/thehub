/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2017 Tom Zander <tomz@freedommail.ch>
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
#include "Application.h"

#include "policy/policy.h"
#include "util.h"
#include "clientversion.h"
#include "chainparams.h"
#include "utilstrencodings.h"
#include "clientversion.h"
#include "net.h"
#include "util.h"

#include "AdminServer.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#ifndef UAHF_CLIENT
# define UAHF_CLIENT 0
#endif


// static
Application * Application::instance()
{
    static Application s_app;
    return &s_app;
}

// static
int Application::exec()
{
    Application *app = Application::instance();
    app->m_threads.join_all();
    return app->m_returnCode;
}

// static
void Application::quit(int rc)
{
    Application *app = Application::instance();
    app->m_returnCode = rc;
    app->m_adminServer.reset();
    app->m_work.reset();
    app->m_ioservice->stop();
    app->m_closingDown = true;
}

Application::Application()
    : m_ioservice(new boost::asio::io_service()),
    m_work(new boost::asio::io_service::work(*m_ioservice)),
    m_returnCode(0),
    m_closingDown(false),
    m_uahfState(UAHFDisabled)
{
    init();
}

void Application::init()
{
    m_closingDown = false;
    for (int i = boost::thread::hardware_concurrency(); i > 0; --i) {
        auto ioservice(m_ioservice);
        m_threads.create_thread([ioservice] {
            RenameThread("Appl-Threadpool");
            while(true) {
                try {
                    ioservice->run();
                    return;
                } catch (const boost::thread_interrupted&) {
                    return;
                } catch (const std::exception& ex) {
                    LogPrintf("Threadgroup: uncaught exception: %s\n", ex.what());
                }
            }
        });
    }

    const bool fallback = GetArg("-uahfstarttime", UAHF_CLIENT) > 0;
    if (GetBoolArg("-uahf", fallback)) {
        m_uahfState = UAHFWaiting;
        const std::string chain = Params().NetworkIDString();
        m_uahfStartTme = chain == CBaseChainParams::REGTEST ? 1296688602 : 1501590000;
    } else {
        m_uahfState = UAHFDisabled;
        m_uahfStartTme = 0;
    }
    logInfo(8002) << "UAHF state:" << m_uahfState << "start time:" << m_uahfStartTme;
}

Application::~Application()
{
}

boost::asio::io_service& Application::ioService()
{
    return *m_ioservice;
}

Admin::Server *Application::adminServer()
{
    if (m_adminServer.get() == nullptr) {
        try {
            m_adminServer.reset(new Admin::Server(*m_ioservice));
        } catch (const std::exception &e) {
            LogPrintf("Can't srart Admin::Server %s\n", e.what());
        }
    }
    return m_adminServer.get();
}

std::string Application::userAgent()
{
    // sanitize comments per BIP-0014, format user agent and check total size
    std::string eb = std::string("EB") + boost::lexical_cast<std::string>(Policy::blockSizeAcceptLimit() / 100000);
    if (eb.at(eb.size()-1) == '0')
        eb = eb.substr(0, eb.size()-1);
    else
        eb.insert(eb.size() - 1, ".", 1);
    std::vector<std::string> comments;
    comments.push_back(eb);
    for (const std::string &comment : mapMultiArgs["-uacomment"]) {
        if (comment == SanitizeString(comment, SAFE_CHARS_UA_COMMENT))
            comments.push_back(comment);
        else
            LogPrintf("User Agent comment (%s) contains unsafe characters.", comment);
    }

    std::ostringstream ss;
    ss << "/";
    ss << clientName() << ":"
       << CLIENT_VERSION_MAJOR << "."
       << CLIENT_VERSION_MINOR << "."
       << CLIENT_VERSION_REVISION;
    if (CLIENT_VERSION_BUILD != 0)
        ss << "." << CLIENT_VERSION_BUILD;
    if (!comments.empty()) {
        auto it(comments.begin());
        ss << "(" << *it;
        for (++it; it != comments.end(); ++it)
            ss << "; " << *it;
        ss << ")";
    }
    ss << "/";
    std::string answer = ss.str();
    if (answer.size() > MAX_SUBVERSION_LENGTH) {
        LogPrintf("Total length of network version string (%i) exceeds maximum length (%i). Reduce the number or size of uacomments.",
            answer.size(), MAX_SUBVERSION_LENGTH);
        answer = answer.substr(0, MAX_SUBVERSION_LENGTH);
    }
    return answer;
}

const char *Application::clientName()
{
    return "Classic";
}

bool Application::closingDown()
{
    return instance()->m_closingDown;
}

Application::UAHFState Application::uahfChainState()
{
    return Application::instance()->m_uahfState;
}

void Application::setUahfChainState(Application::UAHFState state)
{
    logInfo(8002) << "Set UAHF:" << state;
    Application::instance()->m_uahfState = state;
}

int64_t Application::uahfStartTime()
{
    return Application::instance()->m_uahfStartTme;
}
