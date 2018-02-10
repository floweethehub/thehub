project (hub-cli)

include_directories(. ${LIBS_INCLUDES})

set (CLI_LIBS
    flowee_server
    flowee_console
    flowee_utils
    flowee_crypto

    ${UNIVALUE_LIBRARY}
    ${OPENSSL_LIBRARIES}
    ${Boost_LIBRARIES}
    ${Event_LIBRARIES}
    ${BOOST_THREAD_LIBRARY}
)

set (CLI_SOURCES
    bitcoin-cli.cpp
)
if (WIN32)
    list (APPEND CLI_SOURCES bitcoin-cli-res.rc)
endif ()

add_executable(hub-cli ${CLI_SOURCES})

target_link_libraries(hub-cli ${CLI_LIBS})
add_dependencies(hub univalue)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/hub-cli DESTINATION bin)
