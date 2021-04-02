/*
 * This file is part of the Flowee project
 * Copyright (C) 2021 Tom Zander <tom@flowee.org>
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
#include "DiskSpaceChecker.h"
#include "init.h"
#include <Logger.h>
#include <util.h>

#include <boost/filesystem.hpp>


DiskSpaceChecker::DiskSpaceChecker(boost::asio::io_service &io_service)
    : m_timer(io_service)
{
}

bool DiskSpaceChecker::enoughSpaceAvailable()
{
    gatherInfo();
    for (const auto &fs : m_filesystems) {
        assert(!fs.results.empty());
        if (fs.minFree > fs.results.front().available)
            return false;
    }
    return true;
}

void DiskSpaceChecker::start()
{
    m_timer.expires_from_now(boost::posix_time::seconds(5));
    m_timer.async_wait(std::bind(&DiskSpaceChecker::doCheck, this, std::placeholders::_1));
}

void DiskSpaceChecker::gatherInfo()
{
    // first fill them if they don't yet exist.
    if (m_filesystems.empty()) {
        auto datadir = GetDataDir();
        auto blocksDir = datadir / "blocks/";
        auto blocksInfo = space(blocksDir);
        auto utxoDir = datadir / "unspent/";
        auto utxoInfo = space(utxoDir);

        FileSystem fs;
        fs.path = blocksDir.string();
        fs.minFree = 1100000000;
        Result result;
        result.available = blocksInfo.available;
        result.timestamp = time(nullptr);
        fs.results.push_back(result);
        m_filesystems.push_back(fs);

        // same Filesystem
        if (blocksInfo.available == utxoInfo.available) {
            m_filesystems.back().minFree += 500000000;
        }
        else {
            fs.path = utxoDir.string();
            fs.minFree = 500000000;
            fs.results[0].available = utxoInfo.available;
            m_filesystems.push_back(fs);
        }

        // TODO logging space ?
        return;
    }

    // Check history to see if we need to do a new measurement.
    for (auto &fs : m_filesystems) {
        if (needsCheck(fs)) {
            auto blocksInfo = space(boost::filesystem::path(fs.path));
            Result result;
            result.timestamp = time(nullptr);
            result.available = blocksInfo.available;
            fs.results.insert(fs.results.begin(), result);
            if (fs.results.size() > 10)
                fs.results.resize(10);
        }
    }
}

bool DiskSpaceChecker::needsCheck(const DiskSpaceChecker::FileSystem &fs)
{
    assert(!fs.results.empty());

    const auto now = time(nullptr);
    int diff = 120; // no results means we ask once every 2 min.
    // belowe we will check history to maybe check less often.
    for (auto &r : fs.results) {
        if (r.timestamp < now - 19 * 60) {
            // if, in the last minutes, we see the amount of diskspace go down by XXX megabytes,
            // then slow down the checks.
            const auto spaceEaten = fs.results.front().available - r.available;
            if (spaceEaten < 20000000) // < 20MB
                diff = 600;
            else if (spaceEaten < 100000000) // 20 < used < 100MB
                diff = 240;
            else // >= 100MB
                diff = 90;
            break;
        }
    }
    return fs.results.front().timestamp + diff < now;
}

void DiskSpaceChecker::doCheck(const boost::system::error_code &error)
{
    if (error)
        return;

    gatherInfo();
    for (const auto &fs : m_filesystems) {
        assert(!fs.results.empty());
        if (fs.minFree > fs.results.front().available) {
            logFatal(Log::DB) << "DiskSpaceChecker noticed insufficient free space for" << fs.path << "Shutting down";
            StartShutdown();
            return;
        }
        if (fs.minFree * 2 > fs.results.front().available) {
            logCritical(Log::DB).nospace() << "We are getting low on disk space. Please fix! (data on: " << fs.path << ")";
        }
    }

    m_timer.expires_from_now(boost::posix_time::seconds(60));
    m_timer.async_wait(std::bind(&DiskSpaceChecker::doCheck, this, std::placeholders::_1));
}
