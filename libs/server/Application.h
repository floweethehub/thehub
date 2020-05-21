/*
 * This file is part of the Flowee project
 * Copyright (C) 2016 Tom Zander <tomz@freedommail.ch>
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
#ifndef APPLICATION_H
#define APPLICATION_H


#include <WorkerThreads.h>
#include <boost/asio/io_service.hpp>
#include <boost/thread.hpp>

#include <memory>

namespace Validation {
    class Engine;
}
class CTxMemPool;

#define flApp Application::instance()

/**
 * An application singleton that manages some application specific data.
 * The Application singleton can be used to have a single place that owns a threadpool
 * which becomes easy to reach from all your classes and thus avoids cross-dependencies.
 * The IoService is lazy-initialized on first call to IoService() and as such you
 * won't have any negative side-effects if the application does not use them (yet).
 */
class Application : public WorkerThreads
{
public:
    Application();
    ~Application();

    /// returns (and optionally creates) an instance
    static Application *instance();

    static int exec();

    static void quit(int rc = 0);

    Validation::Engine* validation();

    CTxMemPool* mempool();

    /**
     * @brief userAgent creates the user-agent string as it is send over the wire.
     * This includes the client name, the version number and any parameters
     * like -uacomments (user-agent-comments)
     */
    static std::string userAgent();

    /**
     * @returns the name of the client, in this case "Flowee".
     */
    static const char * clientName();

    /**
     * Wrapper function that allows users to create a thread on our global thread-group.
     */
    template<typename F>
    static boost::thread* createThread(F threadfunc) {
        return instance()->createNewThread(threadfunc);
    }

    static bool isClosingDown();

protected:
    void init();
    std::unique_ptr<Validation::Engine> m_validationEngine;

private:
    int m_returnCode;
    std::atomic_bool m_closingDown;
};

#endif
