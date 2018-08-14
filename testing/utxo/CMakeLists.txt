# This file is part of the Flowee project
# Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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

project (test_utxo)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

include_directories(${LIBS_INCLUDES}
    ${CMAKE_CURRENT_BINARY_DIR}
    ..
    ${Qt5Core_INCLUDE_DIRS}
    ${Qt5Test_INCLUDE_DIRS}
)

add_executable(test_utxo
    ../common/TestFlowee.cpp
    test_utxo.cpp
)

target_link_libraries(test_utxo
    flowee_server
    flowee_utils
    flowee_utxo

    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    Qt5::Test
)

add_test(NAME HUB_test_utxo COMMAND test_utxo)
