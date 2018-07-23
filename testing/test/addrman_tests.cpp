/*
 * This file is part of the Flowee project
 * Copyright (C) 2012-2015 The Bitcoin Core developers
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
#include "addrman.h"
#include "test/test_bitcoin.h"
#include <boost/test/unit_test.hpp>
#include <streams.h>
#include <clientversion.h>

class CAddrManTest : public CAddrMan{};

BOOST_FIXTURE_TEST_SUITE(addrman_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(addrman_simple)
{
    CAddrManTest addrman;

    // Set addrman addr placement to be deterministic.
    addrman.MakeDeterministic();

    CNetAddr source = CNetAddr("252.2.2.2:8333");

    // Test 1: Does Addrman respond correctly when empty.
    BOOST_CHECK(addrman.size() == 0);
    CAddrInfo addr_null = addrman.Select();
    BOOST_CHECK(addr_null.ToString() == "[::]:0");

    // Test 2: Does Addrman::Add work as expected.
    CService addr1 = CService("250.1.1.1:8333");
    addrman.Add(CAddress(addr1), source);
    BOOST_CHECK(addrman.size() == 1);
    CAddrInfo addr_ret1 = addrman.Select();
    BOOST_CHECK(addr_ret1.ToString() == "250.1.1.1:8333");

    // Test 3: Does IP address deduplication work correctly. 
    //  Expected dup IP should not be added.
    CService addr1_dup = CService("250.1.1.1:8333");
    addrman.Add(CAddress(addr1_dup), source);
    BOOST_CHECK(addrman.size() == 1);


    // Test 5: New table has one addr and we add a diff addr we should
    //  have two addrs.
    CService addr2 = CService("250.1.1.2:8333");
    addrman.Add(CAddress(addr2), source);
    BOOST_CHECK(addrman.size() == 2);

    // Test 6: AddrMan::Clear() should empty the new table. 
    addrman.Clear();
    BOOST_CHECK(addrman.size() == 0);
    CAddrInfo addr_null2 = addrman.Select();
    BOOST_CHECK(addr_null2.ToString() == "[::]:0");
}

BOOST_AUTO_TEST_CASE(addrman_ports)
{
    CAddrManTest addrman;

    // Set addrman addr placement to be deterministic.
    addrman.MakeDeterministic();

    CNetAddr source = CNetAddr("252.2.2.2:8333");

    BOOST_CHECK(addrman.size() == 0);

    // Test 7; Addr with same IP but diff port does not replace existing addr.
    CService addr1 = CService("250.1.1.1:8333");
    addrman.Add(CAddress(addr1), source);
    BOOST_CHECK(addrman.size() == 1);

    CService addr1_port = CService("250.1.1.1:8334");
    addrman.Add(CAddress(addr1_port), source);
    BOOST_CHECK(addrman.size() == 1);
    CAddrInfo addr_ret2 = addrman.Select();
    BOOST_CHECK(addr_ret2.ToString() == "250.1.1.1:8333");

    // Test 8: Add same IP but diff port to tried table, it doesn't get added.
    //  Perhaps this is not ideal behavior but it is the current behavior.
    addrman.Good(CAddress(addr1_port));
    BOOST_CHECK(addrman.size() == 1);
    bool newOnly = true;
    CAddrInfo addr_ret3 = addrman.Select(newOnly);
    BOOST_CHECK(addr_ret3.ToString() == "250.1.1.1:8333");
}


BOOST_AUTO_TEST_CASE(addrman_select)
{
    CAddrManTest addrman;

    // Set addrman addr placement to be deterministic.
    addrman.MakeDeterministic();

    CNetAddr source = CNetAddr("252.2.2.2:8333");

    // Test 9: Select from new with 1 addr in new.
    CService addr1 = CService("250.1.1.1:8333");
    addrman.Add(CAddress(addr1), source);
    BOOST_CHECK(addrman.size() == 1);

    bool newOnly = true;
    CAddrInfo addr_ret1 = addrman.Select(newOnly);
    BOOST_CHECK(addr_ret1.ToString() == "250.1.1.1:8333");


    // Test 10: move addr to tried, select from new expected nothing returned.
    addrman.Good(CAddress(addr1));
    BOOST_CHECK(addrman.size() == 1);
    CAddrInfo addr_ret2 = addrman.Select(newOnly);
    BOOST_CHECK(addr_ret2.ToString() == "[::]:0");

    CAddrInfo addr_ret3 = addrman.Select();
    BOOST_CHECK(addr_ret3.ToString() == "250.1.1.1:8333");
}

BOOST_AUTO_TEST_CASE(addrman_new_collisions)
{
    CAddrManTest addrman;

    // Set addrman addr placement to be deterministic.
    addrman.MakeDeterministic();

    CNetAddr source = CNetAddr("252.2.2.2:8333");

    BOOST_CHECK(addrman.size() == 0);

    for (unsigned int i = 1; i < 4; i++){
        CService addr = CService("250.1.1."+std::to_string(i));
        addrman.Add(CAddress(addr), source);

        //Test 11: No collision in new table yet.
        BOOST_CHECK(addrman.size() == i);
    }

    //Test 12: new table collision!
    CService addr1 = CService("250.1.1.4");
    addrman.Add(CAddress(addr1), source);
    BOOST_CHECK(addrman.size() == 3);

    CService addr2 = CService("250.1.1.5");
    addrman.Add(CAddress(addr2), source);
    BOOST_CHECK(addrman.size() == 4);
}

BOOST_AUTO_TEST_CASE(addrman_tried_collisions)
{
    CAddrManTest addrman;

    // Set addrman addr placement to be deterministic.
    addrman.MakeDeterministic();

    CNetAddr source = CNetAddr("252.2.2.2:8333");

    BOOST_CHECK(addrman.size() == 0);

    for (unsigned int i = 1; i < 75; i++){
        CService addr = CService("250.1.1."+std::to_string(i));
        addrman.Add(CAddress(addr), source);
        addrman.Good(CAddress(addr));

        //Test 13: No collision in tried table yet.
        // BOOST_TEST_MESSAGE(addrman.size());
        BOOST_CHECK(addrman.size() == i);
    }

    //Test 14: tried table collision!
    CService addr1 = CService("250.1.1.76");
    addrman.Add(CAddress(addr1), source);
    BOOST_CHECK(addrman.size() == 74);

    CService addr2 = CService("250.1.1.77");
    addrman.Add(CAddress(addr2), source);
    BOOST_CHECK(addrman.size() == 75);
}

BOOST_AUTO_TEST_CASE(addrman_serialization)
{
    std::vector<char> data;
    {// save
        CAddrManTest addrman;

        // Set addrman addr placement to be deterministic.
        addrman.MakeDeterministic();

        CNetAddr source = CNetAddr("252.2.2.2:8333");

        for (unsigned int i = 1; i < 75; ++i) {
            CService addr = CService("250.1.1."+std::to_string(i));
            bool ok = addrman.Add(CAddress(addr), source);
            BOOST_CHECK(ok);
            addrman.Good(CAddress(addr));
            if (i == 23 || i == 67) {
                struct in_addr pipv4Addr;
                addr.GetInAddr(&pipv4Addr);
                CAddrInfo *info = addrman.Find(CNetAddr(pipv4Addr));
                BOOST_CHECK(info);
                info->setKnowsXThin(true);
            }
            //Test 13: No collision in tried table yet.
            // BOOST_TEST_MESSAGE(addrman.size());
            BOOST_CHECK(addrman.size() == i);
        }
        BOOST_CHECK_EQUAL(addrman.size(), 74);
        CDataStream ssPeers(SER_DISK, CLIENT_VERSION);
        // ssPeers << FLATDATA(Params().MessageStart());
        ssPeers << addrman;
        data = std::vector<char>(ssPeers.begin(), ssPeers.end());
    }

    { // load
        CAddrManTest addrman;
        CDataStream ssPeers(data, SER_DISK, CLIENT_VERSION);
        ssPeers >> addrman;

        BOOST_CHECK_EQUAL(addrman.size(), 74);
        CNetAddr addr1("250.1.1.23");
        CNetAddr addr2("250.1.1.67");
        CNetAddr addr3("250.1.1.2");
        BOOST_CHECK(addrman.Find(addr1));
        BOOST_CHECK_EQUAL(addrman.Find(addr1)->getKnowsXThin(), true);
        BOOST_CHECK(addrman.Find(addr2));
        BOOST_CHECK_EQUAL(addrman.Find(addr2)->getKnowsXThin(), true);
        BOOST_CHECK(addrman.Find(addr3));
        BOOST_CHECK_EQUAL(addrman.Find(addr3)->getKnowsXThin(), false);
    }
}


BOOST_AUTO_TEST_SUITE_END()
