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
#include "WorkerThreads.h"
#include "util.h"
#include "Logger.h"

WorkerThreads::WorkerThreads()
{
    startThreads();
}

void WorkerThreads::startThreads()
{
    m_ioservice = std::make_shared<boost::asio::io_service>();
    m_work.reset(new boost::asio::io_service::work(*m_ioservice));
    for (int i = boost::thread::hardware_concurrency() + 1; i > 0; --i) {
        auto ioservice(m_ioservice);
        m_threads.create_thread([ioservice] {
            RenameThread("Worker-threads");
            while(true) {
                try {
                    ioservice->run();
                    return;
                } catch (const boost::thread_interrupted&) {
                    return;
                } catch (const std::exception& ex) {
                    logCritical(Log::Bitcoin) << "Threadgroup: uncaught exception" << ex;
                }
            }
        });
    }
}

WorkerThreads::~WorkerThreads()
{
    stopThreads();
}

void WorkerThreads::stopThreads()
{
    m_work.reset();
    if (m_ioservice.get()) // it gets reset() by joinAll
        m_ioservice->stop();
}

void WorkerThreads::joinAll()
{
    m_threads.join_all();
    m_ioservice.reset(); // tasks don't get garbage-collected until the destructor is ran
}

boost::asio::io_service& WorkerThreads::ioService()
{
    return *m_ioservice;
}
