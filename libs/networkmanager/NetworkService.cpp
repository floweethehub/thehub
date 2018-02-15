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
#include "NetworkService.h"
#include "NetworkManager.h"

NetworkService::NetworkService(int id)
    : m_id(id)
{
}

NetworkService::~NetworkService()
{
    if (m_manager)
        m_manager->removeService(this);
}

NetworkManager *NetworkService::manager() const
{
    return m_manager;
}

void NetworkService::setManager(NetworkManager *manager)
{
    m_manager = manager;
}
