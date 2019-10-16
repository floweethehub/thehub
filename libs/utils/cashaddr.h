/*
 * This file is part of the Flowee project
 * Copyright (C) 2017 Pieter Wuille
 * Copyright (C) 2017 The Bitcoin developers
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

// Cashaddr is an address format based on bech32.

#include <streaming/ConstBuffer.h>

#include <cstdint>
#include <string>
#include <vector>

namespace CashAddress {

/**
 * Encode a cashaddr string. Returns the empty string in case of failure.
 */
std::string encode(const std::string &prefix, const std::vector<uint8_t> &values);

/**
 * Decode a cashaddr string. Returns (prefix, data). Empty prefix means failure.
 */
std::pair<std::string, std::vector<uint8_t>> decode(const std::string &str, const std::string &default_prefix);

enum AddressType : uint8_t { PUBKEY_TYPE = 0, SCRIPT_TYPE = 1 };

struct Content {
    AddressType type;
    std::vector<uint8_t> hash;
};

std::string encodeCashAddr(const std::string &prefix, const Content &content);
Content decodeCashAddrContent(const std::string &addr, const std::string &prefix);
std::vector<uint8_t> packCashAddrContent(const Content &content);

/**
 * create a default script for the type and has the output-script
 * to get a unique ID for the entire out-script.
 *
 * \param content the previously parsed content.
 * \return the sha256 hash
 */
Streaming::ConstBuffer createHashedOutputScript(const Content &content);

} // namespace CashAddress
