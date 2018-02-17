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
#ifndef WORKERTHREADS_H
#define WORKERTHREADS_H

#include <boost/asio/io_service.hpp>
#include <boost/thread.hpp>

class WorkerThreads
{
public:
    WorkerThreads();
    ~WorkerThreads();

    void stopThreads();
    void joinAll();

    boost::asio::io_service& ioService();

    /**
     * Wrapper function that allows users to create a thread on our thread-group.
     */
    template<typename F>
    boost::thread* createNewThread(F threadfunc) {
        return m_threads.create_thread(threadfunc);
    }

protected:
    /// only called from constructor. Useful in unit tests.
    void startThreads();

private:
    std::shared_ptr<boost::asio::io_service> m_ioservice;
    std::unique_ptr<boost::asio::io_service::work> m_work;
    boost::thread_group m_threads;
};

#endif
