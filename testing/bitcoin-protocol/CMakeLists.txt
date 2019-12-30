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

project (bitcoin-protocol)
include (testlib)

# for some reason someone thought it was useful to compile the json files into the exe
# instead of just reading them at runtime.
# lets emulate that for now.
find_program(HEXDUMP "hexdump")
find_program(SED "sed")
foreach (JSON_FILE
    data/script_valid.json
    data/script_invalid.json
    data/tx_invalid.json
    data/tx_valid.json)

    # generate 'sighash' array from 'data/sighash.json'
    get_filename_component(JSON_NAME ${JSON_FILE} NAME_WE)
    if (EXISTS "${CMAKE_CURRENT_BINARY_DIR}/${JSON_FILE}.h")
        continue()
    endif ()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/${JSON_FILE}.h "namespace json_tests{ static unsigned const char ${JSON_NAME}[] = {")
    # generate a C++ compatible hex format of the input file.
    execute_process(COMMAND ${HEXDUMP} -v -e "8/1 \"0x%02x, \"" -e "\"\n\"" ${CMAKE_CURRENT_SOURCE_DIR}/${JSON_FILE}
        OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/${JSON_FILE}.hex
    )
    execute_process(COMMAND ${SED} -e "s/0x  ,//g"
        INPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/${JSON_FILE}.hex
        OUTPUT_VARIABLE HEXDATA)
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/${JSON_FILE}.h "${HEXDATA}};};")
endforeach ()


add_executable(test_protocol
    bip32_tests.cpp
    checkdatasig_tests.cpp
    multisig_tests.cpp
    pow_tests.cpp
    uint256_tests.cpp
    script_P2SH_tests.cpp
    script_tests.cpp
    scriptnum_tests.cpp
    transaction_tests.cpp
    transaction_utils.cpp
    DoubleSpendProofTest.cpp
    main.cpp
)
target_link_libraries(test_protocol
    flowee_testlib_server
    flowee_server
    flowee_utils

    ${LEVELDB_LIBRARY}
    ${SECP256K1_LIBRARY}
    ${UNIVALUE_LIBRARY}
    ${OPENSSL_LIBRARIES}
    ${Boost_LIBRARIES}
    ${BERKELEY_DB_LIBRARIES}
    ${MINIUPNP_LIBRARY}
)
add_test(NAME HUB_test_protocol COMMAND test_protocol)
