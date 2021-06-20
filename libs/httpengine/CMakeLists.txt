# This file is part of Flowee
#
# Copyright (C) 2017 Nathan Osman
# Copyright (C) 2019 Tom Zander <tom@flowee.org>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# For the full copy of the License see <http://www.gnu.org/licenses/>
#
project (flowee-httpengine)

set(CMAKE_AUTOMOC ON)

# this can be build in static or in dynamic-library modes.

option(httpengine_static "Build the httpengine library static" ON)

set (PROJECT_VERSION_MAJOR 1)
set (PROJECT_VERSION_MINOR 0)
set (PROJECT_VERSION_PATCH 0)
set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})

set(HEADERS
    basicauthmiddleware.h
    ibytearray.h
    localauthmiddleware.h
    localfile.h
    middleware.h
    parser.h
    proxyhandler.h
    qiodevicecopier.h
    qobjecthandler.h
    range.h
    server.h
    socket.h
    "${CMAKE_CURRENT_BINARY_DIR}/httpengine_export.h"
)

set(SRC
    filesystemhandler.cpp
    basicauthmiddleware.cpp
    handler.cpp
    parser.cpp
    range.cpp
    server.cpp
    middleware.cpp
    socket.cpp
    qiodevicecopier.cpp
    localauthmiddleware.cpp
    localfile.cpp
    qobjecthandler.cpp
    proxyhandler.cpp
    proxysocket.cpp
)

if (httpengine_static)
    include_directories(${CMAKE_CURRENT_BINARY_DIR})
    add_library(flowee_httpengine STATIC ${SRC})
    install (FILES ${HEADERS} DESTINATION include/httpengine/)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libflowee_httpengine.a DESTINATION lib)
else ()
    add_definitions(-DBUILD_SHARED_LIBS)
    add_library(flowee_httpengine SHARED ${SRC})
    set_target_properties(flowee_httpengine PROPERTIES
        CXX_STANDARD          11
        CXX_STANDARD_REQUIRED ON
        DEFINE_SYMBOL         QT_NO_SIGNALS_SLOTS_KEYWORDS
        DEFINE_SYMBOL         HTTPENGINE_LIBRARY
        PUBLIC_HEADER         "${HEADERS}"
        VERSION               ${CLIENT_VERSION_MAJOR}
        SOVERSION             ${HUB_SERIES}
    )

    target_include_directories(flowee_httpengine PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
        "$<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>"
    )
    install(TARGETS flowee_httpengine EXPORT httpengine-export
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        PUBLIC_HEADER DESTINATION "include/httpengine"
    )

    install(EXPORT httpengine-export
        FILE        httpengineConfig.cmake
        DESTINATION "lib/cmake/httpengine"
    )
    include(CMakePackageConfigHelpers)

    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/httpengineConfigVersion.cmake"
        VERSION       ${HUB_SERIES}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/httpengineConfigVersion.cmake"
        DESTINATION "lib/cmake/httpengine"
    )

    configure_file(httpengine.pc.in "${CMAKE_CURRENT_BINARY_DIR}/httpengine.pc" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/httpengine.pc"
        DESTINATION "lib/pkgconfig"
    )
endif ()

configure_file(httpengine_export.h.in "${CMAKE_CURRENT_BINARY_DIR}/httpengine_export.h")
target_link_libraries(flowee_httpengine Qt5::Network)
