# This file is part of the Flowee project
# Copyright (C) 2020 Tom Zander <tomz@freedommail.ch>
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

cmake_minimum_required(VERSION 3.13)
project(univalue)

# Generate univalue_escapes.h
add_executable(univalue_gen gen/gen.cpp)
target_include_directories(univalue_gen PUBLIC include)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/univalue_escapes.h
	COMMAND univalue_gen > ${CMAKE_CURRENT_BINARY_DIR}/univalue_escapes.h
    DEPENDS univalue_gen
)

add_library(univalue
	lib/univalue.cpp
	lib/univalue_read.cpp
	lib/univalue_write.cpp
)
target_sources(univalue PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/univalue_escapes.h)

target_include_directories(univalue
	PUBLIC
		include
	PRIVATE
		lib
        ${CMAKE_CURRENT_BINARY_DIR}
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libunivalue.a DESTINATION lib)
install(FILES include/univalue.h DESTINATION include/flowee)
