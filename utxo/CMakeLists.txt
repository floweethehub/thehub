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

project (utxo-research)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# set the default log section (for logDebug() with no args) to the POS value
add_definitions(-DLOG_DEFAULT_SECTION=19000)

include_directories(. ${LIBS_INCLUDES})

set (UTXO_LIBS
    flowee_server
    flowee_utils
    flowee_utxo

    ${LEVELDB_LIBRARY}
    ${SECP256K1_LIBRARY}
    ${UNIVALUE_LIBRARY}
    ${OPENSSL_LIBRARIES}
    ${Boost_LIBRARIES}
    ${ZMQ_LIBRARIES}
    ${Event_LIBRARIES}
    ${BERKELEY_DB_LIBRARIES}
    ${MINIUPNP_LIBRARY}
    ${BOOST_THREAD_LIBRARY}
)

set (UTXO_SOURCES
    main.cpp
    Importer.cpp
)


find_package(Qt5Concurrent)
if (NOT ${Qt5Concurrent_FOUND})
    message("Missing qt5 (concurrent) library, not building utxo research app")
endif()

if (${Qt5Concurrent_FOUND})
    add_executable(utxo-research ${UTXO_SOURCES})

    target_link_libraries(utxo-research ${UTXO_LIBS} Qt5::Core Qt5Concurrent)
endif ()


install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/utxo-research DESTINATION bin)
