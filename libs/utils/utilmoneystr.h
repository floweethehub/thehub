/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2015 The Bitcoin Core developers
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

/**
 * Money parsing/formatting utilities.
 */
#ifndef FLOWEE_UTILMONEYSTR_H
#define FLOWEE_UTILMONEYSTR_H

#include <cstdint>
#include <string>

typedef int64_t int64_t;

std::string FormatMoney(int64_t n);
bool ParseMoney(const std::string& str, int64_t& nRet);
bool ParseMoney(const char* pszIn, int64_t& nRet);

#endif
