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

project (test)

include_directories(.. ${LIBS_INCLUDES} ${CMAKE_CURRENT_BINARY_DIR})

set (TEST_LIBS
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
    Threads::Threads
)

set (TEST_SOURCES
    ../flowee_tests.cpp
    address_manager_tests.cpp
    addrman_tests.cpp
    alert_tests.cpp
    allocator_tests.cpp
    arith_uint256_tests.cpp
    base32_tests.cpp
    base58_tests.cpp
    base64_tests.cpp
    bip32_tests.cpp
    blocksdb_mapfile_tests.cpp
    blocksdb_tests.cpp
    blockvalidation_tests.cpp
    bloom_tests.cpp
    bswap_tests.cpp
    Checkpoints_tests.cpp
    coins_tests.cpp
    compress_tests.cpp
    crypto_tests.cpp
    dbwrapper_tests.cpp
    DoS_tests.cpp
    double_spend.cpp
    getarg_tests.cpp
    hash_tests.cpp
    key_tests.cpp
    limitedmap_tests.cpp
    main_tests.cpp
    mempool_tests.cpp
    merkle_tests.cpp
    miner_tests.cpp
    multisig_tests.cpp
    netbase_tests.cpp
    pmt_tests.cpp
    policyestimator_tests.cpp
    pow_tests.cpp
    rpc_tests.cpp
    sanity_tests.cpp
    scriptnum_tests.cpp
    script_P2SH_tests.cpp
    script_tests.cpp
    serialize_tests.cpp
    sighash_tests.cpp
    sigopcount_tests.cpp
    skiplist_tests.cpp
    streams_tests.cpp
    test_bitcoin.cpp
    TestBuffers.cpp
    TestConnections.cpp
    thinblock_tests.cpp
    timedata_tests.cpp
    transaction_tests.cpp
    transaction_utils.cpp
    txvalidationcache_tests.cpp
    uahf_tests.cpp
    uint256_tests.cpp
    univalue_tests.cpp
    util_tests.cpp
    versionbits_tests.cpp
)

# for some reason someone thought it was useful to compile the json files into the exe
# instead of just reading them at runtime.
# lets emulate that for now.
find_program(HEXDUMP "hexdump")
find_program(SED "sed")
foreach (JSON_FILE
    data/script_valid.json
    data/base58_keys_valid.json
    data/base58_encode_decode.json
    data/base58_keys_invalid.json
    data/script_invalid.json
    data/tx_invalid.json
    data/tx_valid.json
    data/sighash.json)

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

if (enable_wallet)
    set (TEST_SOURCES_WALLET
        accounting_tests.cpp
        ${CMAKE_SOURCE_DIR}/libs/server/wallet/test/wallet_tests.cpp
        rpc_wallet_tests.cpp
        ${CMAKE_SOURCE_DIR}/libs/server/wallet/test/crypto_tests.cpp
    )
endif ()

add_executable(test_hub ${TEST_SOURCES} ${TEST_SOURCES_WALLET})

target_link_libraries(test_hub ${TEST_LIBS})
add_dependencies(hub univalue)
add_dependencies(hub leveldb)
add_dependencies(hub secp256k1)

add_test(NAME HUB_tests COMMAND test_hub)
