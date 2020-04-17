/*
 * This file is part of the Flowee project
 * Copyright (C) 2012-2014 The Bitcoin Core developers
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

#ifndef FLOWEE_VERSION_H
#define FLOWEE_VERSION_H

/**
 * network protocol versioning
 */

constexpr int PROTOCOL_VERSION = 70012;

//! initial proto version, to be increased after version/verack negotiation
constexpr int INIT_PROTO_VERSION = 209;

//! In this version, 'getheaders' was introduced.
constexpr int GETHEADERS_VERSION = 31800;

//! nTime field added to CAddress, starting with this version;
//! if possible, avoid requesting addresses nodes older than this
constexpr int CADDR_TIME_VERSION = 31402;

//! only request blocks from nodes outside this range of versions
constexpr int NOBLKS_VERSION_START = 32000;
constexpr int NOBLKS_VERSION_END = 32400;

//! BIP 0031, pong message, is enabled for all versions AFTER this one
constexpr int BIP0031_VERSION = 60000;

//! "mempool" command, enhanced "getdata" behavior starts with this version
constexpr int MEMPOOL_GD_VERSION = 60002;

//! "sendheaders" command and announcing blocks with headers starts with this version
constexpr int SENDHEADERS_VERSION = 70012;

//! "feefilter" tells peers to filter invs to you by fee starts with this
//! version
constexpr int FEEFILTER_VERSION = 70013;

//! short-id-based block download starts with this version
constexpr int SHORT_IDS_BLOCKS_VERSION = 70014;

//! not banning for invalid compact blocks starts with this version
constexpr int INVALID_CB_NO_BAN_VERSION = 70015;

//! Expedited Relay enabled in this version
constexpr int EXPEDITED_VERSION = 80002;


//! disconnect from peers older than this proto version
constexpr int MIN_PEER_PROTO_VERSION = SENDHEADERS_VERSION;

#endif
