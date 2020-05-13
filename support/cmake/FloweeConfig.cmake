# - Try to find Flowee libraries
# Once done this will define
#
#  FLOWEE_FOUND - system has Flowee
#  FLOWEE_INCLUDE_DIR - path to flowee includes
#
# Additionally, the following libraries will be made available;
#  flowee_interfaces
#  flowee_utils
#  flowee_crypto
#  flowee_utils
#  flowee_apputils
#  flowee_networkmanager
#  flowee_p2p
#  flowee_httpengine

# Copyright (c) 2019-2020 Tom Zander <tomz@freedommail.ch>


find_path(FLOWEE_INCLUDE_DIR flowee/utils/WorkerThreads.h
)

set (__libsFound "NOT_FOUND")

find_library(_FloweeSecp256k1 libsecp256k1.a ${FLOWEE_INCLUDE_DIR}/..)
if (_FloweeSecp256k1)
    add_library(secp256k1 STATIC IMPORTED)
    set_property(TARGET secp256k1 PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${FLOWEE_INCLUDE_DIR}/flowee)
    set_property(TARGET secp256k1 PROPERTY IMPORTED_LINK_INTERFACE_LIBRARIES flowee_crypto flowee_interfaces)
    set_target_properties(secp256k1 PROPERTIES IMPORTED_LOCATION ${_FloweeSecp256k1})
    set (__libsFound "secp256k1")
endif()
include (${CMAKE_CURRENT_LIST_DIR}/secp256k1.cmake)

find_library(_floweeInterfaces libflowee_interfaces.a ${FLOWEE_INCLUDE_DIR}/..)
if (_floweeInterfaces)
    add_library(flowee_interfaces STATIC IMPORTED)
    set_property(TARGET flowee_interfaces PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${FLOWEE_INCLUDE_DIR}/flowee/interfaces)
    set_property(TARGET flowee_interfaces APPEND PROPERTY INTERFACE_COMPILE_OPTIONS -fPIC)
    set_target_properties(flowee_interfaces PROPERTIES IMPORTED_LOCATION ${_floweeInterfaces})
    set (__libsFound "${__libsFound} flowee_interfaces")
endif()

find_library(_FloweeCrypto libflowee_crypto.a ${FLOWEE_INCLUDE_DIR}/..)
if (_FloweeCrypto)
    add_library(flowee_crypto STATIC IMPORTED)
    set_property(TARGET flowee_crypto PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${FLOWEE_INCLUDE_DIR}/flowee/crypto)
    set_property(TARGET flowee_crypto PROPERTY IMPORTED_LINK_INTERFACE_LIBRARIES secp256k1)
    set_target_properties(flowee_crypto PROPERTIES IMPORTED_LOCATION "${_FloweeCrypto}")
    set (__libsFound "${__libsFound} flowee_crypto")
endif()

find_library(_FloweeUtils libflowee_utils.a ${FLOWEE_INCLUDE_DIR}/..)
if (_FloweeUtils)
    add_library(flowee_utils STATIC IMPORTED)
    set_property(TARGET flowee_utils PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${FLOWEE_INCLUDE_DIR}/flowee/utils/)
    set_property(TARGET flowee_utils PROPERTY IMPORTED_LINK_INTERFACE_LIBRARIES flowee_crypto flowee_interfaces)
    set_target_properties(flowee_utils PROPERTIES IMPORTED_LOCATION "${_FloweeUtils}")
    set (__libsFound "${__libsFound} flowee_utils")
endif()

find_library(_FloweeAppUtils libflowee_apputils.a ${FLOWEE_INCLUDE_DIR}/..)
if (_FloweeAppUtils)
    add_library(flowee_apputils STATIC IMPORTED)
    set_property(TARGET flowee_apputils PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${FLOWEE_INCLUDE_DIR}/flowee/)
    set_property(TARGET flowee_apputils PROPERTY IMPORTED_LINK_INTERFACE_LIBRARIES flowee_utils flowee_networkmanager)
    set_target_properties(flowee_apputils PROPERTIES IMPORTED_LOCATION "${_FloweeAppUtils}")
    set (__libsFound "${__libsFound} flowee_apputils")
endif()

find_library(_FloweeNetworkManager libflowee_networkmanager.a ${FLOWEE_INCLUDE_DIR}/..)
if (_FloweeNetworkManager)
    add_library(flowee_networkmanager STATIC IMPORTED)
    set_property(TARGET flowee_networkmanager PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${FLOWEE_INCLUDE_DIR}/flowee/networkmanager)
    set_property(TARGET flowee_networkmanager PROPERTY IMPORTED_LINK_INTERFACE_LIBRARIES flowee_utils)
    set_target_properties(flowee_networkmanager PROPERTIES IMPORTED_LOCATION "${_FloweeNetworkManager}")
    set (__libsFound "${__libsFound} flowee_networkmanager")
endif()

find_library(_FloweeP2PNet libflowee_p2p.a ${FLOWEE_INCLUDE_DIR}/..)
if (_FloweeP2PNet)
    add_library(flowee_p2p STATIC IMPORTED)
    set_property(TARGET flowee_p2p PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${FLOWEE_INCLUDE_DIR}/flowee/p2p)
    set_property(TARGET flowee_p2p PROPERTY IMPORTED_LINK_INTERFACE_LIBRARIES flowee_utils flowee_networkmanager)
    set_target_properties(flowee_p2p PROPERTIES IMPORTED_LOCATION "${_FloweeP2PNet}")
    set (__libsFound "${__libsFound} flowee_p2p")
endif()

find_library(_FloweeHttpEngine libflowee_httpengine.a ${FLOWEE_INCLUDE_DIR}/..)
if (_FloweeHttpEngine)
    add_library(flowee_httpengine STATIC IMPORTED)
    set_property(TARGET flowee_httpengine PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${FLOWEE_INCLUDE_DIR}/httpengine)
    set_target_properties(flowee_httpengine PROPERTIES IMPORTED_LOCATION "${_FloweeHttpEngine}")
    set (__libsFound "${__libsFound} flowee_httpengine")
endif()

if (__libsFound)
    add_definitions(-DHAVE_CONFIG_H)
    if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
        add_definitions(-DBCH_NO_DEBUG_OUTPUT)
    else ()
        add_definitions(-DBCH_LOGCONTEXT)
    endif ()
    message ("Found Flowee ${CMAKE_CURRENT_LIST_FILE}. Found components: ${__libsFound}")
    set (FLOWEE_FOUND TRUE)
else ()
    if (Flowee_FIND_REQUIRED)
        message(FATAL_ERROR "-- Required component Flowee not found")
    else ()
        message ("No Flowee libs found on system")
    endif ()
endif()
unset(__libsFound)

