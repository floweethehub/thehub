/*
 * This file is part of the Flowee project
 * Copyright (C) 2020-2021 Tom Zander <tom@flowee.org>
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
#include "Action.h"
#include "DownloadManager.h"

#include <functional>

Action::Action(DownloadManager *parent)
    : m_dlm(parent),
    m_timer(parent->service())
{
}

void Action::again()
{
    m_timer.expires_from_now(boost::posix_time::milliseconds(m_interval));
    m_timer.async_wait(m_dlm->strand().wrap(std::bind(&Action::execute, this, std::placeholders::_1)));
}

void Action::setInterval(int milliseconds)
{
    m_interval = milliseconds;
}

void Action::start()
{
    assert(m_dlm);
    m_dlm->strand().post(std::bind(&Action::execute, this, boost::system::error_code()));
}

void Action::cancel()
{
    m_timer.cancel();
}

Action::~Action()
{
}
