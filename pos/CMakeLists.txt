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

project (pos)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5Core)

include_directories(. ${LIBS_INCLUDES})

set (POS_LIBS
    flowee_networkmanager
    flowee_utils

    ${OPENSSL_LIBRARIES}
    ${Boost_LIBRARIES}
    ${BOOST_THREAD_LIBRARY}
)

set (POS_SOURCES
    main.cpp
    NetworkPaymentProcessor.cpp
)

add_executable(pos ${POS_SOURCES})

target_link_libraries(pos ${POS_LIBS} Qt5::Core)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/pos DESTINATION bin)
