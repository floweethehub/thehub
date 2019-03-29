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

project (hub-cli)

include_directories(. ${LIBS_INCLUDES})
include_directories(${LIBSERVER_INCLUDES} ${LIBCONSOLE_INCLUDES})


set (CLI_LIBS
    flowee_server
    flowee_console
    flowee_utils
    flowee_crypto

    ${UNIVALUE_LIBRARY}
    ${OPENSSL_LIBRARIES}
    ${Boost_LIBRARIES}
    ${Event_LIBRARIES}
    ${BOOST_THREAD_LIBRARY}
)

set (CLI_SOURCES
    bitcoin-cli.cpp
)
if (WIN32)
    list (APPEND CLI_SOURCES bitcoin-cli-res.rc)
endif ()

add_executable(hub-cli ${CLI_SOURCES})

target_link_libraries(hub-cli ${CLI_LIBS})
add_dependencies(hub univalue)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/hub-cli DESTINATION bin)
