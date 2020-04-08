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

include(ExternalProject)

ExternalProject_Add(univalue
    PREFIX ${CMAKE_BINARY_DIR}/lib
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/univalue
    CONFIGURE_COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/univalue/configure  --disable-shared --with-pic --libdir=${CMAKE_BINARY_DIR}/lib --includedir=${CMAKE_BINARY_DIR}/include
    TEST_AFTER_INSTALL test/unitester
    DEPENDEES autogen
)

ExternalProject_Add_Step(univalue autogen
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/univalue/autogen.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/univalue
    COMMENT "Creating univalue configure"
)

ExternalProject_Add(leveldb
    #PREFIX ${CMAKE_BINARY_DIR}/lib
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/leveldb
    BUILD_COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/leveldb all memenv_test
    CONFIGURE_COMMAND echo building leveldb in src dir
    INSTALL_COMMAND cp -a "${CMAKE_CURRENT_SOURCE_DIR}/leveldb/${CMAKE_STATIC_LIBRARY_PREFIX}memenv${CMAKE_STATIC_LIBRARY_SUFFIX}" "${CMAKE_CURRENT_SOURCE_DIR}/leveldb/${CMAKE_STATIC_LIBRARY_PREFIX}leveldb${CMAKE_STATIC_LIBRARY_SUFFIX}" "${CMAKE_BINARY_DIR}/lib/" && cp -Ra "${CMAKE_CURRENT_SOURCE_DIR}/leveldb/include/leveldb" "${CMAKE_CURRENT_SOURCE_DIR}/leveldb/helpers/memenv/memenv.h" "${CMAKE_BINARY_DIR}/include/"
)

add_subdirectory(secp256k1)
