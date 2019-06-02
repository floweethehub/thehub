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

#include "utilstrencodings.h"
#include "clientversion.h"
#include "net.h"
#include "util.h"

#include <validation/Engine.h>


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
    app->m_closingDown = true;
    app->m_validationEngine.reset();
    app->stopThreads();
}

Application::Application()
    : m_returnCode(0),
    m_closingDown(false)
{
    init();
}

void Application::init()
{
    m_closingDown = false;
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
            logCritical(Log::Bitcoin).nospace() << "User Agent comment (" << comment << ") contains unsafe characters.";
    }

    std::ostringstream ss;
    ss << "/";
    ss << clientName() << ":" << HUB_SERIES;
    if (!comments.empty()) {
        auto it(comments.begin());
        ss << "(" << *it;
        for (++it; it != comments.end(); ++it)
            ss << "; " << *it;
        ss << ")";
    } else {
        ss << " (" << CLIENT_VERSION_MAJOR << "-" << CLIENT_VERSION_MINOR << ")";
    }
    ss << "/";
    std::string answer = ss.str();
    if (answer.size() > MAX_SUBVERSION_LENGTH) {
        logCritical(Log::Bitcoin).nospace() << "Total length of network version string (" << answer.size()
                                            << ") exceeds maximum length (" << MAX_SUBVERSION_LENGTH
                                            << "). Reduce the number or size of uacomments.";
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
