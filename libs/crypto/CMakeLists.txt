# This file is part of the Flowee project
# Copyright (C) 2018-2019 Tom Zander <tom@flowee.org>
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

include_directories(${LIBCRYPTO_INCLUDES} ${CMAKE_BINARY_DIR}/include)

set (FLOWEE_LIBRARY_SOURCES
    aes.cpp
    hmac_sha256.cpp
    hmac_sha512.cpp
    ripemd160.cpp
    sha1.cpp
    sha256.cpp
    sha512.cpp
)

include(CheckCSourceRuns)
set (CMAKE_REQUIRED_FLAGS "-msse4 -msse4.1")
check_c_source_runs("#include <stdint.h>
#include <immintrin.h>
int main() { __m128i l = _mm_set1_epi32(0); return _mm_extract_epi32(l, 3); }" SSE41)
if (${SSE41})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4 -msse4.1 -DENABLE_SSE41")
    list(APPEND FLOWEE_LIBRARY_SOURCES sha256_sse41.cpp)
endif()

set (CMAKE_REQUIRED_FLAGS "-msse4 -msha")
check_c_source_runs("#include <stdint.h>
#include <immintrin.h>
int main() {
  __m128i i = _mm_set1_epi32(0);
  __m128i j = _mm_set1_epi32(1);
  __m128i k = _mm_set1_epi32(2);
  return _mm_extract_epi32(_mm_sha256rnds2_epu32(i, i, k), 0);
}" SHANI)
if (${SHANI})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4 -msha -DENABLE_SHANI")
    list(APPEND FLOWEE_LIBRARY_SOURCES sha256_shani.cpp)
endif()

set (CMAKE_REQUIRED_FLAGS "-mavx -mavx2")
check_c_source_runs("#include <stdint.h>
#include <immintrin.h>
    int main() {__m256i l = _mm256_set1_epi32(0); return _mm256_extract_epi32(l, 7);}" AVX2)
if (${AVX2})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx -mavx2 -DENABLE_AVX2")
    list(APPEND FLOWEE_LIBRARY_SOURCES sha256_avx2.cpp)
endif()

add_library(flowee_crypto STATIC ${FLOWEE_LIBRARY_SOURCES})

# allow usage in dlls
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libflowee_crypto.a DESTINATION lib)
install(FILES
    common.h
    ripemd160.h
    sha256.h
DESTINATION include/flowee/crypto)
install(FILES
    compat/endian.h
    compat/byteswap.h
DESTINATION include/flowee/crypto/compat/)
# this is really from the top-level cmake, but as this is the top-level library, we install it here
install(FILES
    ${CMAKE_BINARY_DIR}/include/config/flowee-config.h
DESTINATION include/flowee/config/)
