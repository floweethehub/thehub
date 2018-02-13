/*
 * This file is part of the Flowee project
 * Copyright (C) 2016 Tom Zander <tomz@freedommail.ch>
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

#ifndef FLOWEE_TEST_TRANSACTION_UTILS_H
#define FLOWEE_TEST_TRANSACTION_UTILS_H

#include <vector>
#include <string>

class CScript;
struct CMutableTransaction;
class CTransaction;

namespace TxUtils {
    void RandomScript(CScript &script);
    void RandomInScript(CScript &script);

    enum RandomTransactionType {
        SingleOutput,
        AnyOutputCount
    };

    void RandomTransaction(CMutableTransaction &tx, RandomTransactionType single);

    // create one transaction and repeat it until it fills up the space.
    std::vector<CTransaction> transactionsForBlock(int minSize);

    std::string FormatScript(const CScript& script);
}

#endif
