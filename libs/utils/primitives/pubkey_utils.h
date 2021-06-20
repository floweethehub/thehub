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
#ifndef PUBKEY_UTILS_H
#define PUBKEY_UTILS_H

#include <vector>
#include <cstdint>

// utils for public keys. Use this file to avoid the secp256k1 dependency
namespace PubKey {
    bool isValidSize(const std::vector<uint8_t> &vch);

    //! Compute the length of a pubkey with a given first byte.
    unsigned int keyLength(unsigned char chHeader);
}

#endif
