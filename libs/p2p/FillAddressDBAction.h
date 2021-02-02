/*
 * This file is part of the Flowee project
 * Copyright (C) 2020 Tom Zander <tom@flowee.org>
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
#ifndef FILLADDRESSDBACTION_H
#define FILLADDRESSDBACTION_H

#include "Action.h"

#include <boost/asio.hpp>

class FillAddressDBAction : public Action
{
public:
    FillAddressDBAction(DownloadManager *parent);

protected:
    void execute(const boost::system::error_code &error) override;

private:
    void onAddressResolveComplete(const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator iterator);

    int64_t m_lastRequestStarted = 0;

    int m_dnsLookupState = -1;
    boost::asio::ip::tcp::resolver m_resolver;

    const std::vector<std::string> m_seeders;
};

#endif
