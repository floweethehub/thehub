/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2015 The Bitcoin Core developers
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
#ifndef MUTABLETRANSACTIONSIGNATURECHECKER_H
#define MUTABLETRANSACTIONSIGNATURECHECKER_H

#include <script/interpreter.h>
#include <primitives/transaction.h>
#include <amount.h>


class MutableTransactionSignatureChecker : public TransactionSignatureChecker
{
public:
    MutableTransactionSignatureChecker(const CMutableTransaction *txToIn, unsigned int nInIn, const CAmount &amount)
        : TransactionSignatureChecker(&txTo, nInIn, amount), txTo(*txToIn) {
    }

private:
    const CTransaction txTo;
};

#endif
