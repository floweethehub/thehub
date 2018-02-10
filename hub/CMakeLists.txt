project (hub)

include_directories(. ${LIBS_INCLUDES})

set (HUB_LIBS
    flowee_api
    flowee_networkmanager
    flowee_server
    flowee_console
    flowee_utils
    flowee_crypto

    ${LEVELDB_LIBRARY}
    ${SECP256K1_LIBRARY}
    ${UNIVALUE_LIBRARY}
    ${OPENSSL_LIBRARIES}
    ${Boost_LIBRARIES}
    ${ZMQ_LIBRARIES}
    ${Event_LIBRARIES}
    ${BERKELEY_DB_LIBRARIES}
    ${MINIUPNP_LIBRARY}
    ${BOOST_THREAD_LIBRARY}
)

set (HUB_SOURCES
    bitcoind.cpp
)
if (WIN32)
    list (APPEND HUB_SOURCES bitcoind-res.rc)
endif ()


add_executable(hub ${HUB_SOURCES})

target_link_libraries(hub ${HUB_LIBS})
add_dependencies(hub univalue)
add_dependencies(hub leveldb)
add_dependencies(hub secp256k1)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/hub DESTINATION bin)
