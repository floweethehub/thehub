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

project (hub)

include_directories(${LIBAPI_INCLUDES} ${LIBCONSOLE_INCLUDES})

set (HUB_LIBS
    flowee_api
    flowee_server
    flowee_networkmanager
    flowee_console
    flowee_utils
    flowee_crypto

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

set (HUB_SOURCES
    bitcoind.cpp
)
if (WIN32)
    list (APPEND HUB_SOURCES bitcoind-res.rc)
endif ()


add_executable(hub ${HUB_SOURCES})

target_link_libraries(hub ${HUB_LIBS})
add_dependencies(hub univalue)
add_dependencies(hub leveldb)
add_dependencies(hub secp256k1)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/hub DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/support/flowee.conf
    CONFIGURATIONS Release RelWithDebInfo
    DESTINATION etc/flowee)
install(FILES ${CMAKE_SOURCE_DIR}/support/thehub.service
    CONFIGURATIONS Release RelWithDebInfo
    DESTINATION systemd/system/)
