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
#ifndef TEST_UTXO_H
#define TEST_UTXO_H

#include <common/TestFloweeBase.h>
#include <utxo/UnspentOutputDatabase.h>
#include <uint256.h>

#include <boost/filesystem.hpp>

class TestUtxo : public TestFloweeBase
{
    Q_OBJECT
public:
    TestUtxo() {}

private slots:
    void init();
    void cleanup();

    void basic();
    void multiple();
    void restart();
    void commit();

private:
    void insertTransactions(UnspentOutputDatabase &db, int number);
    uint256 insertedTxId(int index);
    boost::filesystem::path m_testPath;
    const char *templateTxId = "0x1234517444b051c44dfd2720e88f314ff94f3dd6d56d40ef65854fcfd7ff6b%02x";
};

#endif
