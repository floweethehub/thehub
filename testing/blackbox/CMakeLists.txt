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

set (abs_top_builddir ${CMAKE_BINARY_DIR})
set (abs_top_srcdir ${CMAKE_SOURCE_DIR})
if (WIN32)
    set (EXEEXT ".exe")
else ()
    set (EXEEXT "")
endif ()

if (NOT enable_wallet)
    set (ENABLE_WALLET_TRUE "#")
endif ()

configure_file (
    "${CMAKE_CURRENT_SOURCE_DIR}/pull-tester/tests_config.py.in"
    ${CMAKE_CURRENT_BINARY_DIR}/pull-tester/tests_config.py
    @ONLY
)

configure_file (
    "${CMAKE_CURRENT_SOURCE_DIR}/pull-tester/run-bitcoind-for-test.sh.in"
    ${CMAKE_CURRENT_BINARY_DIR}/pull-tester/run-bitcoind-for-test.sh
    @ONLY
)

file (COPY ${CMAKE_CURRENT_SOURCE_DIR}/pull-tester/rpc-tests.py
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/pull-tester/rpc-tests.py)
