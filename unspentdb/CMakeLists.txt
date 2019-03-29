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

project (unspent-db)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# set the default log section (for logDebug() with no args) to the POS value
add_definitions(-DLOG_DEFAULT_SECTION=11000)

include_directories(. ${LIBUTXO_INCLUDES} ${LIBSERVER_INCLUDES})

add_executable(unspent-db
    main.cpp
    AbstractCommand.cpp
    CheckCommand.cpp
    ExportCommand.cpp
    InfoCommand.cpp
    LookupCommand.cpp
    PruneCommand.cpp
)

target_link_libraries(unspent-db
    flowee_utils
    flowee_utxo
    flowee_server

    ${LEVELDB_LIBRARY}
    ${OPENSSL_LIBRARIES}
    ${Boost_LIBRARIES}
    ${BOOST_THREAD_LIBRARY}
    Qt5::Core
)


install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/unspent-db DESTINATION bin)
