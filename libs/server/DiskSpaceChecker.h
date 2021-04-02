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
#ifndef DISKSPACECHECKER_H
#define DISKSPACECHECKER_H

#include <boost/asio/io_service.hpp>
#include <boost/asio/deadline_timer.hpp>

class DiskSpaceChecker
{
public:
    DiskSpaceChecker(boost::asio::io_service &io_service);

    bool enoughSpaceAvailable();

    void start();

private:
    struct Result {
        int64_t available;
        int64_t timestamp;
    };
    struct FileSystem {
        std::string path;
        int minFree;
        std::vector<Result> results;
    };

    void gatherInfo();
    bool needsCheck(const FileSystem &fs);
    void doCheck(const boost::system::error_code &error);

    boost::asio::deadline_timer m_timer;
    std::vector<FileSystem> m_filesystems;
};

#endif
