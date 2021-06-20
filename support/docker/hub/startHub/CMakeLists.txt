# This file is part of the Flowee project
# Copyright (C) 2018 Tom Zander <tom@flowee.org>
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

cmake_minimum_required (VERSION 3.13)
project (StartHub)

find_package(Qt5Core REQUIRED)
if (${Qt5Core_FOUND})
    message("-- Qt5 found, version ${Qt5Core_VERSION_STRING}")
endif()

add_executable(startHub main.cpp)
target_link_libraries(startHub pthread Qt5::Core)

