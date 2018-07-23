/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2018 Tom Zander <tomz@freedommail.ch>
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
#include "chainparams.h"
#include "utilstrencodings.h"
#include "clientversion.h"
#include "net.h"
#include "util.h"

#include <validation/Engine.h>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()


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
    app->joinAll();
    return app->m_returnCode;
}

// static
void Application::quit(int rc)
{
    Application *app = Application::instance();
    app->m_returnCode = rc;
    app->m_validationEngine.reset();
    app->stopThreads();
    app->m_closingDown = true;
}

Application::Application()
    : m_returnCode(0),
    m_closingDown(false),
    m_uahfState(UAHFDisabled)
{
    init();
}

void Application::init()
{
    m_closingDown = false;
    m_uahfState = UAHFWaiting;
    const std::string chain = Params().NetworkIDString();
    if (chain == CBaseChainParams::REGTEST) {
        m_uahfStartTme = 1296688602;
        m_uahfState = UAHFActive;
    } else {
        m_uahfStartTme = 1501590000;
    }
}

Application::~Application()
{
}

Validation::Engine *Application::validation()
{
    if (m_validationEngine.get() == nullptr)
        m_validationEngine.reset(new Validation::Engine());
    return m_validationEngine.get();
}

CTxMemPool *Application::mempool()
{
    return validation()->mempool();
}

std::string Application::userAgent()
{
    std::vector<std::string> comments;
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
       << CLIENT_VERSION_MINOR;
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
    return "Flowee";
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
    Application::instance()->m_uahfState = state;
}

int64_t Application::uahfStartTime()
{
    return Application::instance()->m_uahfStartTme;
}
