/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tom@flowee.org>
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

#include "pubkey_utils.h"

bool PubKey::isValidSize(const std::vector<uint8_t> &vch)
{
    return vch.size() > 0 && keyLength(vch[0]) == vch.size();
}

//! Compute the length of a pubkey with a given first byte.
unsigned int PubKey::keyLength(unsigned char chHeader)
{
    if (chHeader == 2 || chHeader == 3)
        return 33;
    if (chHeader == 4 || chHeader == 6 || chHeader == 7)
        return 65;
    return 0;
}
