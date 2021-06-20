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

project (test_httpengine)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_BINARY_DIR}/libs/httpengine
    ${Qt5Core_INCLUDE_DIRS}
    ${Qt5Test_INCLUDE_DIRS}
)

find_package(Qt5Network REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(test_httpengine
    common/qsimplehttpclient.cpp
    common/qsocketpair.cpp
    TestBasicAuthMiddleware.cpp
    TestFilesystemHandler.cpp
    TestHandler.cpp
    TestLocalAuthMiddleware.cpp
    TestLocalFile.cpp
    TestMiddleware.cpp
    TestParser.cpp
    TestQIODeviceCopier.cpp
    TestRange.cpp
    TestServer.cpp
    TestSocket.cpp
    resource.qrc
    main.cpp
)
target_link_libraries(test_httpengine
    flowee_httpengine
    Qt5::Test
)
add_test(NAME HUB_test_httpengine COMMAND test_httpengine)
