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
project(Leveldb)

# This project can take advantage of C++11.
set(CMAKE_CXX_STANDARD 11)

include(CheckIncludeFileCXX)
check_include_file_cxx("atomic" LEVELDB_ATOMIC_PRESENT)

include_directories(.)

# Leveldb library
add_library(leveldb
	db/builder.cc
	db/c.cc
	db/dbformat.cc
	db/db_impl.cc
	db/db_iter.cc
	db/dumpfile.cc
	db/filename.cc
	db/log_reader.cc
	db/log_writer.cc
	db/memtable.cc
	db/repair.cc
	db/table_cache.cc
	db/version_edit.cc
	db/version_set.cc
	db/write_batch.cc
	table/block_builder.cc
	table/block.cc
	table/filter_block.cc
	table/format.cc
	table/iterator.cc
	table/merger.cc
	table/table_builder.cc
	table/table.cc
	table/two_level_iterator.cc
	util/arena.cc
	util/bloom.cc
	util/cache.cc
	util/coding.cc
	util/comparator.cc
	util/crc32c.cc
	util/env.cc
	util/env_posix.cc
	util/filter_policy.cc
	util/hash.cc
	util/histogram.cc
	util/logging.cc
	util/options.cc
	util/status.cc
    helpers/memenv/memenv.cc
)

# Select the proper port: posix or Windows.
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(LEVELDB_PLATFORM WINDOWS)
	set(LEVELDB_OS WINDOWS)
	target_sources(leveldb
		PRIVATE
			util/env_win.cc
			port/port_win.cc
	)
	target_compile_definitions(leveldb
		PRIVATE
			WINVER=0x0500
			__USE_MINGW_ANSI_STDIO=1
	)

	find_package(SHLWAPI REQUIRED)
	target_link_libraries(leveldb ${SHLWAPI_LIBRARY})
	target_include_directories(leveldb PUBLIC ${SHLWAPI_INCLUDE_DIR})
else()
	set(LEVELDB_PLATFORM POSIX)
	target_sources(leveldb PRIVATE port/port_posix.cc)

	set(THREADS_PREFER_PTHREAD_FLAG ON)
	find_package(Threads REQUIRED)
	target_link_libraries(leveldb Threads::Threads)

	if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		set(LEVELDB_OS LINUX)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
		set(LEVELDB_OS MACOSX)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "(Solaris|SunOS)")
		set(LEVELDB_OS SOLARIS)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
		set(LEVELDB_OS FREEBSD)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "KFreeBSD")
		set(LEVELDB_OS KFREEBSD)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "NetBSD")
		set(LEVELDB_OS NETBSD)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD")
		set(LEVELDB_OS OPENBSD)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "DragonFly")
		set(LEVELDB_OS DRAGONFLYBSD)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "Android")
		set(LEVELDB_OS ANDROID)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "HPUX")
		# No idea what's the proper system name is here.
		set(LEVELDB_OS HPUX)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "iOS")
		# No idea what's the proper system name is here.
		set(LEVELDB_OS IOS)
	else()
		message(FATAL_ERROR "Cannot build leveldb for ${CMAKE_SYSTEM_NAME}. Please file a bug report.")
	endif()
endif()

# Right now this is not used but the latest version of leveldb uses this
# so we might as well be ready for it.
if (HAVE_CRC32C)
  target_link_libraries(leveldb crc32c)
endif (HAVE_CRC32C)
if (HAVE_SNAPPY)
  target_link_libraries(leveldb snappy)
endif (HAVE_SNAPPY)

target_include_directories(leveldb PUBLIC include helpers/memenv)
target_compile_definitions(leveldb
    PUBLIC
        OS_${LEVELDB_OS}
        LEVELDB_PLATFORM_${LEVELDB_PLATFORM}
)
if(LEVELDB_ATOMIC_PRESENT)
    target_compile_definitions(leveldb PUBLIC LEVELDB_ATOMIC_PRESENT)
endif(LEVELDB_ATOMIC_PRESENT)
