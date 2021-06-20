/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2015 The Bitcoin Core developers
 * Copyright (C) 2016 Tom Zander <tom@flowee.org>
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
#ifndef TX_TESTS_H
#define TX_TESTS_H

#include <common/TestFloweeEnvPlusNet.h>

class TransactionTests : public TestFloweeEnvPlusNet
{
    Q_OBJECT
public:
    static unsigned int parseScriptFlags(const std::string &strFlags);
    std::string FormatScriptFlags(unsigned int flags) const;

private slots:
    void tx_valid();
    void tx_invalid();
    void basic_transaction_tests();
    void test_IsStandard();
    void transactionIter();
    void transactionIter2();
};

#endif
