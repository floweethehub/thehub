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
#ifndef ACTION_H
#define ACTION_H

#include <boost/asio/deadline_timer.hpp>

class DownloadManager;

/***
 * The Action is a baseclass for the P2PNet async maintainance actions.
 *
 * Most of the design of the p2p lib is based on events. A peer sends something,
 * we respond.
 * This design makes it really hard to do monitoring like actions, for instance
 * there is no clean way to use events to respond to a peer NOT doing something.
 *
 * This is where actions come in, they are owned by the DownloadManager and
 * run every couple of seconds in order to do things.
 *
 * Any user action can be created and you can reimplement execute() which will
 * get called periodically.
 *
 * To start:
 * @code
 *   DownloadManager::addAction<MyAction>();
 * @endcode
 *
 * To stop call `DownloadManager::done(this);`
 *
 * And please be sure to call 'again()' every single iteration of execute() as long
 * as the action is not done yet.
 */
class Action
{
public:
    virtual ~Action();

    /// This is called by the DownloadManager to call execute() async
    void start();

    /// This is called on system shutdown
    virtual void cancel();

protected:
    explicit Action(DownloadManager *parent);
    virtual void execute(const boost::system::error_code &error) = 0;

    /// Makes the execute() method be called again after an interval.
    void again();
    DownloadManager * const m_dlm;

    /// Set the amount of milliseconds that 'again()' waits
    void setInterval(int milliseconds);

private:
    boost::asio::deadline_timer m_timer;
    int m_interval = 1500;
};

#endif
