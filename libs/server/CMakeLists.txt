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

include_directories(${LIBSERVER_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_BINARY_DIR}/include)

set (FLOWEE_SERVER_FILES
    addrman.cpp
    Application.cpp
    arith_uint256.cpp
    BlocksDB.cpp
    bloom.cpp
    chain.cpp
    chainparams.cpp
    checkpoints.cpp
    compressor.cpp
    consensus/merkle.cpp
    core_read.cpp
    core_write.cpp
    CrashCatcher.cpp
    dbwrapper.cpp
    DoubleSpendProof.cpp
    DoubleSpendProofStorage.cpp
    encodings_legacy.cpp
    httprpc.cpp
    httpserver.cpp
    init.cpp
    keystore.cpp
    main.cpp
    merkleblock.cpp
    miner.cpp
    net.cpp
    noui.cpp
    policy/policy.cpp
    pow.cpp
    primitives/FastUndoBlock.cpp
    protocol.cpp
    rest.cpp
    rpcblockchain.cpp
    rpcmining.cpp
    rpcmisc.cpp
    rpcnet.cpp
    rpcrawtransaction.cpp
    rpcserver.cpp
    scheduler.cpp
    serverutil.cpp
    script/interpreter.cpp
    script/script_error.cpp
    script/sigcache.cpp
    script/sign.cpp
    script/standard.cpp
    thinblock.cpp
    timedata.cpp
    torcontrol.cpp
    txmempool.cpp
    txorphancache.cpp
    UnspentOutputData.cpp
    validation/BlockValidation.cpp
    validation/Engine.cpp
    validation/TxValidation.cpp
    validation/ValidationException.cpp
    validation/ValidationSettings.cpp
    validation/VerifyDB.cpp
)

if (enable_wallet)
    set (FLOWEE_SERVER_WALLET_FILES
        wallet/crypter.cpp
        wallet/db.cpp
        wallet/rpcdump.cpp
        wallet/rpcwallet.cpp
        wallet/wallet.cpp
        wallet/walletdb.cpp
        wallet/wallet_ismine.cpp
    )
endif ()

if (ZMQ_FOUND)
    set (FLOWEE_SERVER_FILES_ZMQ
        zmq/zmqabstractnotifier.cpp
        zmq/zmqnotificationinterface.cpp
        zmq/zmqpublishnotifier.cpp
    )
endif ()

add_library(flowee_server STATIC
    ${FLOWEE_SERVER_FILES}
    ${FLOWEE_SERVER_WALLET_FILES}
    ${FLOWEE_SERVER_FILES_ZMQ}
)

add_dependencies(flowee_server univalue)
add_dependencies(flowee_server secp256k1)
add_dependencies(flowee_server leveldb)

target_link_libraries(flowee_server flowee_utxo)
