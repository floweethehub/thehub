# This file is part of the Flowee project
# Copyright (C) 2019 Tom Zander <tom@flowee.org>
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

# set the default log section (for logDebug() with no args) to the Indexer value
add_definitions(-DLOG_DEFAULT_SECTION=8000)

find_package(Qt5Sql)

include_directories(${LIBAPPUTILS_INCLUDES} ${LIBUTXO_INCLUDES} ${CMAKE_BINARY_DIR}/include)

set (IDX_LIBS
    flowee_utxo
    flowee_networkmanager
    flowee_apputils
    flowee_utils

    ${OPENSSL_LIBRARIES}
    ${Boost_LIBRARIES}
)

if (${Qt5Sql_FOUND})
    add_executable(indexer
        main.cpp
        AddressIndexer.cpp
        HashStorage.cpp
        Indexer.cpp
        SpentOuputIndexer.cpp
        TxIndexer.cpp
    )

    target_link_libraries(indexer ${IDX_LIBS} Qt5::Sql)
    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/indexer DESTINATION bin)
    install(FILES ${CMAKE_SOURCE_DIR}/support/indexer.conf
        CONFIGURATIONS Release RelWithDebInfo
        DESTINATION etc/flowee)
    install(FILES ${CMAKE_SOURCE_DIR}/support/indexer.service
        CONFIGURATIONS Release RelWithDebInfo
        DESTINATION systemd/system/)
endif ()

add_executable(indexer-cli cli.cpp
    IndexerClient.cpp
)
target_link_libraries(indexer-cli ${IDX_LIBS})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/indexer-cli DESTINATION bin)
