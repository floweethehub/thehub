set (abs_top_builddir ${CMAKE_BINARY_DIR})
set (abs_top_srcdir ${CMAKE_SOURCE_DIR})
if (WIN32)
    set (EXEEXT ".exe")
else ()
    set (EXEEXT "")
endif ()

if (NOT enable_wallet)
    set (ENABLE_WALLET_TRUE "#")
endif ()

configure_file (
    "${CMAKE_CURRENT_SOURCE_DIR}/pull-tester/tests_config.py.in"
    ${CMAKE_CURRENT_BINARY_DIR}/pull-tester/tests_config.py
    @ONLY
)

configure_file (
    "${CMAKE_CURRENT_SOURCE_DIR}/pull-tester/run-bitcoind-for-test.sh.in"
    ${CMAKE_CURRENT_BINARY_DIR}/pull-tester/run-bitcoind-for-test.sh
    @ONLY
)

file (COPY ${CMAKE_CURRENT_SOURCE_DIR}/pull-tester/rpc-tests.py
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/pull-tester/rpc-tests.py)
