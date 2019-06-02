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
    NetworkManager.cpp
    NetworkServiceBase.cpp
    NetworkService.cpp
    NetworkException.cpp
    NetworkQueueFullError.cpp
)

target_link_libraries(flowee_networkmanager flowee_utils)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libflowee_networkmanager.a DESTINATION lib)

install(FILES
    NetworkConnection.h
    NetworkEndPoint.h
    NetworkManager.h
DESTINATION include)
