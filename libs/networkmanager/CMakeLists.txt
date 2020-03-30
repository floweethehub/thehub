# This file is part of the Flowee project
# Copyright (C) 2018-2019 Tom Zander <tomz@freedommail.ch>
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

include_directories(${LIBNETWORKMANAGER_INCLUDES} ${CMAKE_BINARY_DIR}/include)

add_library(flowee_networkmanager STATIC
    NetworkConnection.cpp
    NetworkEndPoint.cpp
    NetworkManager.cpp
    NetworkServiceBase.cpp
    NetworkService.cpp
    NetworkException.cpp
    NetworkQueueFullError.cpp
)

# allow usage in dlls
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libflowee_networkmanager.a DESTINATION lib)

install(FILES
    NetworkConnection.h
    NetworkEndPoint.h
    NetworkException.h
    NetworkManager.h
    NetworkQueueFullError.h
    NetworkServiceBase.h
    NetworkService.h
DESTINATION include/flowee/networkmanager)
