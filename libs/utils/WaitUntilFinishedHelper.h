/*
 * This file is part of the Flowee project
 * Copyright (C) 2017-2018 Tom Zander <tom@flowee.org>
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

#include <functional>
#include <boost/asio/io_context_strand.hpp>

#include <mutex>
#include <atomic>

/**
 * @brief Use the WaitUntilFinishedHelper class to start a method in a strand and wait until its done.
 * Usage is simple, you create an instance and all the work is done in the constructor.
 * After the constructor is finished, your method in the strand will have returned.
 */
class WaitUntilFinishedHelper
{
public:
    WaitUntilFinishedHelper(const std::function<void()> &target, boost::asio::io_context::strand *strand);
    WaitUntilFinishedHelper(const WaitUntilFinishedHelper &other);
    ~WaitUntilFinishedHelper();

    void run();

private:
    struct Private {
        mutable std::mutex mutex;
        std::function<void()> target;
        std::atomic<int> ref;
        boost::asio::io_context::strand *strand;
    };
    Private *d;

    void handle();
};


