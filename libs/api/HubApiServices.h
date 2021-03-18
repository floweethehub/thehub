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
#ifndef HUBAPISERVICES_H
#define HUBAPISERVICES_H

#include "APIServer.h"
#include "AddressMonitorService.h"
#include "BlockNotificationService.h"
#include "TransactionMonitorService.h"
#include "DoubleSpendService.h"


/**
 * A class encapsulating the API services for the Hub.
 * This class helps with maintainance load where we now have one place to create and delete
 * the services, which is automatically done in the right order.
 * Adding a new service will now be a limited change withing the API library.
 */
class HubApiServices
{
public:
    HubApiServices(boost::asio::io_service &service);

private:
    Api::Server m_apiServer;
    TransactionMonitorService m_transactionMonitorService;
    AddressMonitorService m_addressMonitorService;
    BlockNotificationService m_blockNotificationService;
    DoubleSpendService m_dsp;
};

#endif
