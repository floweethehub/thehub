/*
 * This file is part of the Flowee project
 * Copyright (C) 2020 Tom Zander <tomz@freedommail.ch>
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
#include "InventoryItem.h"

InventoryItem::InventoryItem(const uint256 &hash, uint32_t type)
    : m_hash(hash),
    m_type(type)
{
}

uint256 InventoryItem::hash() const
{
    return m_hash;
}

uint32_t InventoryItem::type() const
{
    return m_type;
}

bool InventoryItem::operator==(const InventoryItem &other) const
{
    return other.type() == m_type && other.m_hash == m_hash;
}
