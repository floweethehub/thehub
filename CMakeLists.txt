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

cmake_minimum_required (VERSION 3.13)
project (Flowee)

set(COPYRIGHT_YEAR 2020)
set(CLIENT_VERSION_MAJOR ${COPYRIGHT_YEAR})
set(CLIENT_VERSION_MINOR 1)
set(CLIENT_VERSION_REVISION 1)
set(HUB_SERIES 1)

# ------------------------------------------------------------------------------------

set(CLIENT_VERSION_IS_RELEASE 1)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/support/cmake)

message(STATUS "Flowee version ${CLIENT_VERSION_MAJOR}-${CLIENT_VERSION_MINOR}.${CLIENT_VERSION_REVISION} ${HUB_SERIES}")

set(FLOWEE_GIT_SHA1_STRING "")
set(FLOWEE_GIT_BRANCH_STRING "")

include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)
get_git_branch(GIT_BRANCH)

if (GIT_SHA1 AND GIT_BRANCH)
    string(SUBSTRING ${GIT_SHA1} 0 7 GIT_SHA1)
    set(FLOWEE_GIT_SHA1_STRING ${GIT_SHA1})
    set(FLOWEE_GIT_BRANCH_STRING ${GIT_BRANCH})
endif ()


 ############
#############
## Options ##
#############
############

option(enable_wallet "Compile the legacy wallet" OFF)
if (enable_wallet)
    add_definitions(-DENABLE_WALLET)
endif ()

option(dev_setup "Enable developer build, not safe for real-life usage!" OFF)
option(build_tests "Compile the unit tests" ${dev_setup})
if (dev_setup)
    set(CMAKE_BUILD_TYPE Debug)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DBCH_LOGCONTEXT -DUSE_UNSAFE_RANDOM -DENABLE_CRASH_CATCHER")
    message(STATUS "Setting built type to Debug, with unsafe random and other dev options.")
elseif (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'ReleaseWithDebugInfo' as none was specified.")
    set(CMAKE_BUILD_TYPE RELWITHDEBINFO)
    # The default build should be free from debugging log-statements.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_RELEASE} -DBCH_NO_DEBUG_OUTPUT")
    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo")
elseif (NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Built type is ${CMAKE_BUILD_TYPE}")
endif ()

# turn off debug logging for release builds
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DBCH_NO_DEBUG_OUTPUT")

if (build_tests)
    # add dependency
    set (boost_test_lib "unit_test_framework")
endif ()

option(enable_gui "Compile the GUI client (hub-qt)" ON)

option(mark_release "If true, remove the pre-release warning from the app" OFF)
if (mark_release)
    if (dev_setup)
        message ("ignoring 'mark-release since you turned on dev-setup, they are incompatible")
    else ()
        set(CLIENT_VERSION_IS_RELEASE 1)
    endif ()
endif ()


# required packages below this
message ("===== Find required dependencies")

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl@1.1/")
    set(CMAKE_PREFIX_PATH  ${CMAKE_PREFIX_PATH} "/usr/local/opt/qt")
endif()

find_package(OpenSSL REQUIRED)
message ("Using OpenSSL ${OPENSSL_VERSION}")

find_package(Boost 1.67.0
    REQUIRED
    chrono filesystem iostreams program_options system thread ${boost_test_lib}
)

if (enable_wallet)
    find_package(BerkeleyDB REQUIRED)
    message ("NOTICE: txVulcano is not compiled when wallet is enabled")
endif ()

# Since our 3rd party require this, lets give a nice warning here.
find_program(AUTOCONF autoconf)
if (NOT AUTOCONF)
    message ("ERROR: autoconf is required for 3rd-party libraries compilation.")
endif()

find_package(Event REQUIRED)


message ("===== Find optional dependencies")

find_package(ZeroMQ 4.0)
if (${ZMQ_FOUND})
    set (ENABLE_ZMQ 1)
else ()
    # remove "NOTFOUND" so we can just link to it, which then is a no-op
    set (ZMQ_LIBRARIES "")
endif ()

find_package(Miniupnpc)
if (${MINIUPNP_FOUND})
    set (USE_UPNP 1)
else ()
    set (MINIUPNP_LIBRARY "")
endif ()

find_package(QREncode)
if (${QREncode_FOUND})
    set (USE_QRCODE 1)
else ()
    set (QREncode_LIBRARIES "")
endif()

find_package(Qt5Core)
if (${Qt5Core_FOUND})
    message("-- Qt5 found, version ${Qt5Core_VERSION_STRING}")
    find_package(Qt5Network)
    if (NOT ${Qt5Network_FOUND})
        message("Missing Qt5 (network) library, not building bitcore-proxy")
    endif ()
else()
    message("Missing qt5 (core) library, not building pos, hub-qt, txVulcano")
    set (enable_gui FALSE)
endif()
if (enable_gui)
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        # Qt GUI apps require bundles on Mac, needs work to support
        message("GUIs not supported on MacOS (yet).")
        set (enable_gui FALSE)
    else ()
        find_package(Qt5Widgets)
        if (NOT ${Qt5Widgets_FOUND})
            message("Missing Qt5 (widgets) library, not building hub-qt")
            set (enable_gui FALSE)
        endif ()
    endif ()
endif ()
if (enable_gui)
    find_package(Qt5DBus)
    if (${Qt5DBus_FOUND})
        set (USE_DBUS 1)
    endif ()
endif ()

######################
#### Generate config.h
######################

# first optional stuff so all the non-optional errors are at the bottom;

include(CheckSymbolExists)
find_path (HAVE_BYTESWAP_H byteswap.h)
check_symbol_exists(bswap_16 "byteswap.h" HAVE_DECL_BSWAP_16)
check_symbol_exists(bswap_32 "byteswap.h" HAVE_DECL_BSWAP_32)
check_symbol_exists(bswap_64 "byteswap.h" HAVE_DECL_BSWAP_64)
find_path (HAVE_ENDIAN_H endian.h)
find_path (HAVE_SYS_ENDIAN_H sys/endian.h)
check_symbol_exists(le16toh "endian.h" HAVE_DECL_LE16TOH)
check_symbol_exists(be16toh "endian.h" HAVE_DECL_BE16TOH)
check_symbol_exists(be32toh "endian.h" HAVE_DECL_BE32TOH)
check_symbol_exists(be64toh "endian.h" HAVE_DECL_BE64TOH)
check_symbol_exists(htobe16 "endian.h" HAVE_DECL_HTOBE16)
check_symbol_exists(htobe32 "endian.h" HAVE_DECL_HTOBE32)
check_symbol_exists(htobe64 "endian.h" HAVE_DECL_HTOBE64)
check_symbol_exists(htole16 "endian.h" HAVE_DECL_HTOLE16)
check_symbol_exists(htole32 "endian.h" HAVE_DECL_HTOLE32)
check_symbol_exists(htole64 "endian.h" HAVE_DECL_HTOLE64)
check_symbol_exists(le16toh "endian.h" HAVE_DECL_LE16TOH)
check_symbol_exists(le32toh "endian.h" HAVE_DECL_LE32TOH)
check_symbol_exists(le64toh "endian.h" HAVE_DECL_LE64TOH)
find_path (HAVE_SYS_PRCTL_H sys/prctl.h)
find_path (HAVE_SYS_SELECT_H sys/select.h)
check_symbol_exists(strnlen "string.h" HAVE_DECL_STRNLEN)
if (HAVE_DECL_STRNLEN) #default is 1.
else () # set to zero instead of empty
    set (HAVE_DECL_STRNLEN 0)
endif ()

include(CheckBuiltinExists)
check_builtin_exist(__builtin_popcount HAVE_DECL___BUILTIN_POPCOUNT)

set (WORDS_BIGENDIAN ${CMAKE_WORDS_BIGENDIAN})
include (TestBigEndian)
test_big_endian(CMAKE_WORDS_BIGENDIAN)

include(CheckCXXSourceCompiles)
# older cmake doesn't set C++11 flags, do it ourselves.
set (_V "CPP11_ARGS")
foreach (ARG "" "-std=gnu++11" "-std=c++11")
    set (CMAKE_REQUIRED_FLAGS ${ARG})
    check_cxx_source_compiles(
        "#if __cplusplus < 201103L
        # error
        #endif
        int main() {}
        "
        ${_V})
    if (${_V})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ARG}")
        break()
    endif()
    set (_V "${_V}_")
endforeach()

check_cxx_source_compiles(
    "#include <string.h>
    int main() {
        char buffer[10];
        const char* c = strerror_r(0, buffer, 3);
        (void)c;
    }
    "
    STRERROR_R_CHAR_P)

configure_file (
    "${CMAKE_SOURCE_DIR}/libs/config/flowee-config.h.in"
    ${CMAKE_BINARY_DIR}/include/config/flowee-config.h
    @ONLY
)

execute_process(
    COMMAND support/genbuild.sh ${CMAKE_BINARY_DIR}/include/build.h ${CMAKE_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_definitions(-DHAVE_CONFIG_H -DHAVE_BUILD_INFO)

# no-deprecated is needed for dbwrapper (leveldb)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wvla -pthread -Wno-deprecated ")



######################
#### Include hierarchy
######################

set(UNIVALUE_LIBRARY "${CMAKE_BINARY_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}univalue${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(LEVELDB_LIBRARY
    "${CMAKE_BINARY_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}leveldb${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "${CMAKE_BINARY_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}memenv${CMAKE_STATIC_LIBRARY_SUFFIX}"
)
set(SECP256K1_LIBRARY "${CMAKE_BINARY_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}secp256k1${CMAKE_STATIC_LIBRARY_SUFFIX}")

# define the include dirs for each section
set(3RDPARTY_INCLUDES)
set(LIBCRYPTO_INCLUDES
    ${3RDPARTY_INCLUDES}
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/crypto
    ${CMAKE_SOURCE_DIR}/libs
)
message ("OpenSSL includes: ${OPENSSL_INCLUDE_DIR}")
set(LIBUTILS_INCLUDES ${LIBCRYPTO_INCLUDES} ${CMAKE_SOURCE_DIR}/libs/utils ${CMAKE_SOURCE_DIR}/libs/interfaces)
set(LIBNETWORKMANAGER_INCLUDES ${LIBUTILS_INCLUDES} ${CMAKE_SOURCE_DIR}/libs/networkmanager)
set(LIBAPPUTILS_INCLUDES ${LIBNETWORKMANAGER_INCLUDES} ${CMAKE_SOURCE_DIR}/libs/apputils)
set(LIBUTXO_INCLUDES ${LIBUTILS_INCLUDES} ${CMAKE_SOURCE_DIR}/libs/utxo)
set(LIBHTTPENGINE_INCLUDES ${CMAKE_SOURCE_DIR}/libs/httpengine ${CMAKE_BINARY_DIR}/libs/httpengine)


# these two should only ever be used by the hub hub-qt and the unit tests.
set(LIBSERVER_INCLUDES ${LIBNETWORKMANAGER_INCLUDES} ${CMAKE_SOURCE_DIR}/libs/server ${CMAKE_SOURCE_DIR}/libs/server/wallet)
set(LIBAPI_INCLUDES ${LIBSERVER_INCLUDES} ${CMAKE_SOURCE_DIR}/libs/api)


add_subdirectory(libs)
add_subdirectory(hub)
add_subdirectory(hub-cli)
if (enable_gui)
    add_subdirectory(hub-qt)
endif ()
if (build_tests)
    add_subdirectory(testing)
endif ()
if (${Qt5Core_FOUND})
    if (NOT enable_wallet)
        add_subdirectory(txVulcano)
    endif()
    add_subdirectory(indexer)
    add_subdirectory(pos)
    add_subdirectory(unspentdb)
    if (${Qt5Network_FOUND})
        add_subdirectory(bitcore-proxy)
    endif ()
endif ()

# cmake install file
install(FILES support/cmake/FloweeConfig.cmake DESTINATION lib/cmake/flowee/)
