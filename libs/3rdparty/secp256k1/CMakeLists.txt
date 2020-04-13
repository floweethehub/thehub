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

cmake_minimum_required(VERSION 3.5)

project(secp256k1)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR} src
	${CMAKE_CURRENT_BINARY_DIR}/src
)

## Build gen_context which is used to pre-compute ecmult_static_context.h

add_executable (gen_context src/gen_context.c)
target_link_libraries (gen_context)
target_compile_definitions(gen_context PRIVATE LIBSECP256K1_CONFIG_H)

add_custom_command (
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/src/ecmult_static_context.h
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/gen_context
    DEPENDS gen_context
)

## Generate the config

find_package(GMP)
if(GMP_FOUND)
	set(USE_NUM_GMP 1)
	set(USE_FIELD_INV_NUM 1)
	set(USE_SCALAR_INV_NUM 1)
else()
	set(USE_NUM_NONE 1)
	set(USE_FIELD_INV_BUILTIN 1)
	set(USE_SCALAR_INV_BUILTIN 1)
    set(GMP_LIBRARY "")
    set(GMP_INCLUDE_DIR "")
endif()

# check if amd64 asm is supported.
include(CheckCSourceCompiles)
check_c_source_compiles("
	#include <stdint.h>
	int main() {
		uint64_t a = 11, tmp;
		__asm__ __volatile__(\"movq \$0x100000000,%1; mulq %%rsi\" : \"+a\"(a) : \"S\"(tmp) : \"cc\", \"%rdx\");
		return 0;
	}
" USE_ASM_X86_64)

# make sure __int128 is defined
include(CheckTypeSize)
check_type_size(__int128 SIZEOF___INT128)
if(SIZEOF___INT128 EQUAL 16)
	set(HAVE___INT128 1)
else()
	# If we do not support __int128, we should be falling back
	# on 32bits implementations for field and scalar.
endif()

# Detect if we are on a 32 or 64 bits plateform and chose
# scalar and filed implementation accordingly
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	# 64 bits implementationr require either __int128 or asm support.
	if (HAVE___INT128 OR USE_ASM_X86_64)
		set(USE_SCALAR_4X64 1)
		set(USE_FIELD_5X52 1)
	else()
		message(SEND_ERROR "Compiler does not support __int128 or insline assembly")
	endif()
else()
	set(USE_SCALAR_8X32 1)
	set(USE_FIELD_10X26 1)
endif()
set(ENABLE_MODULE_ECDH 0)
set(ENABLE_MODULE_MULTISET 1)
set(ENABLE_MODULE_RECOVERY 1)
set(ENABLE_MODULE_SCHNORR 1)
set(USE_ECMULT_STATIC_PRECOMPUTATION 1)

configure_file(src/libsecp256k1-config.h.cmake.in src/libsecp256k1-config.h ESCAPE_QUOTES)


## Last, actually compile our library.

add_library(secp256k1 src/secp256k1.c)
# Make sure we build the header
target_sources(secp256k1 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/src/ecmult_static_context.h)
target_include_directories(secp256k1 PUBLIC include ${GMP_INCLUDE_DIR})
target_link_libraries(secp256k1 ${GMP_LIBRARY})
target_compile_definitions(secp256k1 PRIVATE HAVE_CONFIG_H SECP256K1_BUILD)
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wshadow -Wno-unused-function -Wno-nonnull -Wno-overlength-strings -std=c89 -Wno-long-long")
# Default visibility is hidden on all targets.
set(CMAKE_C_VISIBILITY_PRESET hidden)


install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libsecp256k1.a DESTINATION lib)
install(FILES
    include/secp256k1.h
    include/secp256k1_multiset.h
    include/secp256k1_recovery.h
    include/secp256k1_schnorr.h
DESTINATION include/flowee)
