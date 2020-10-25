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

include_directories(${LIBAPI_INCLUDES} ${CMAKE_BINARY_DIR}/include)
add_definitions(-DLOG_DEFAULT_SECTION=2500)

add_library(flowee_api STATIC
    APIRPCBinding.cpp
    APIServer.cpp

    AddressMonitorService.cpp
    BlockNotificationService.cpp
    TransactionMonitorService.cpp
)

target_link_libraries(flowee_api flowee_server flowee_networkmanager)
