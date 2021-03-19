# This file is part of the Flowee project
# Copyright (C) 2019-2021 Tom Zander <tom@flowee.org>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

project (api)
include (testlib)

set(CMAKE_AUTORCC ON)

configure_file(
    blk00000.dat
    ${CMAKE_CURRENT_BINARY_DIR}/blk00000.dat
    COPYONLY)

set (LIBS flowee_testlib flowee_networkmanager flowee_utils ${TEST_LIBS} ${OPENSSL_LIBRARIES})
set (COMMONSRC BlackBoxTest.cpp data.qrc)

##### LIVE

add_executable(test_api_live
    ${COMMONSRC}
    TestLive.cpp
)
target_link_libraries(test_api_live ${LIBS})
add_test(NAME HUB_test_api_live COMMAND test_api_live)

##### Blockchain

add_executable(test_api_blockchain
    ${COMMONSRC}
    TestBlockchain.cpp
)
target_link_libraries(test_api_blockchain ${LIBS})
add_test(NAME HUB_test_api_blockchain COMMAND test_api_blockchain)

##### Address Monitor

add_executable(test_api_address_monitor
    ${COMMONSRC}
    TestAddressMonitor.cpp
    TestData.cpp
)
target_link_libraries(test_api_address_monitor ${LIBS})
add_test(NAME HUB_test_api_address_monitor COMMAND test_api_address_monitor)

##### Double Spend Proof Monitor

add_executable(test_api_double_spend_monitor
    ${COMMONSRC}
    TestData.cpp
    TestDoubleSpendProofMonitor.cpp
)
target_link_libraries(test_api_double_spend_monitor ${LIBS})
add_test(NAME HUB_test_api_double_spend_monitor COMMAND test_api_double_spend_monitor)

##### TXID Monitor

add_executable(test_api_txid_monitor
    ${COMMONSRC}
    TestData.cpp
    TestTxIdMonitor.cpp
)
target_link_libraries(test_api_txid_monitor ${LIBS})
add_test(NAME HUB_test_api_txid_monitor COMMAND test_api_txid_monitor)
