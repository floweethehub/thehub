/*
 * This file is part of the Flowee project
 * Copyright (C) 2017-2018 Tom Zander <tomz@freedommail.ch>
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

#include "WaitUntilFinishedHelper.h"

#include "Logger.h"

WaitUntilFinishedHelper::WaitUntilFinishedHelper(const std::function<void ()> &target, boost::asio::io_context::strand *strand)
    : d(new Private())
{
    d->ref = 1;
    d->target = target;
    d->strand = strand;
}

WaitUntilFinishedHelper::WaitUntilFinishedHelper(const WaitUntilFinishedHelper &other)
    : d(other.d) {
    d->ref++;
}

WaitUntilFinishedHelper::~WaitUntilFinishedHelper() {
    if (!--d->ref)
        delete d;
}

void WaitUntilFinishedHelper::run()
{
    d->mutex.lock();
    d->strand->dispatch(std::bind(&WaitUntilFinishedHelper::handle, this));
    d->mutex.lock();
}

void WaitUntilFinishedHelper::handle() {
    try {
        d->target();
    } catch (const std::exception &e) {
        logFatal(Log::Bitcoin) << "Unhandled exception caught by WaitUntilFinishedHelper" << e;
    }
    d->mutex.unlock();
}
