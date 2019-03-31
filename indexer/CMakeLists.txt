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

project (indexing-server)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

# set the default log section (for logDebug() with no args) to the POS value
add_definitions(-DLOG_DEFAULT_SECTION=8000)

find_package(Qt5Sql)

include_directories(${LIBNETWORKMANAGER_INCLUDES} ${LIBUTXO_INCLUDES})

set (IDX_LIBS
    flowee_utxo
    flowee_networkmanager
    flowee_utils

    ${OPENSSL_LIBRARIES}
    ${Boost_LIBRARIES}
    ${BOOST_THREAD_LIBRARY}
)

if (${Qt5Sql_FOUND})
    add_executable(indexer
        main.cpp
        AddressIndexer.cpp
        FloweeServiceApplication.cpp
        HashStorage.cpp
        Indexer.cpp
        TxIndexer.cpp
    )

    target_link_libraries(indexer ${IDX_LIBS} Qt5::Core Qt5::Sql)

    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/indexer DESTINATION bin)
endif ()
