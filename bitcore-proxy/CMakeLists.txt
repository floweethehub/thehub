# This file is part of the Flowee project
# Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
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

project (bitcore-proxy)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# set the default log section (for logDebug() with no args) to the bitcore-proxy value
add_definitions(-DLOG_DEFAULT_SECTION=9100)

# enable this to get a per-request timing printed in the log.
add_definitions(-DBENCH)

include_directories(${LIBAPPUTILS_INCLUDES} ${LIBHTTPENGINE_INCLUDES} ${CMAKE_BINARY_DIR}/include)

add_executable(bitcore-proxy
    main.cpp
    BitcoreProxy.cpp
    webdata.qrc
)
set (LIBS
    flowee_apputils
    flowee_networkmanager
    flowee_utils
    flowee_httpengine

    ${Boost_LIBRARIES}
    Qt5::Core
    Qt5::Network
)
target_link_libraries(bitcore-proxy ${LIBS})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/bitcore-proxy DESTINATION bin)

###################

add_executable(bitcore-proxy-tester TestApi.cpp)
target_link_libraries(bitcore-proxy-tester ${LIBS})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/bitcore-proxy-tester DESTINATION bin)
