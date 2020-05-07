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

include_directories(${LIBUTILS_INCLUDES} ${CMAKE_BINARY_DIR}/include)

add_library(flowee_utils STATIC
    amount.cpp
    arith_uint256.cpp
    base58.cpp
    bloom.cpp
    cashaddr.cpp
    chainparamsbase.cpp
    clientversion.cpp
    compat/glibc_sanity.cpp
    compat/glibcxx_sanity.cpp
    compat/strnlen.cpp
    hash.cpp
    LogChannels.cpp
    Logger.cpp
    Message.cpp
    merkle.cpp
    merkleblock.cpp
    PartialMerkleTree.cpp
    random.cpp
    streaming/BufferPool.cpp
    streaming/ConstBuffer.cpp
    streaming/MessageBuilder.cpp
    streaming/MessageParser.cpp
    streaming/P2PBuilder.cpp
    streaming/P2PParser.cpp
    support/cleanse.cpp
    support/pagelocker.cpp
    sync.cpp
    TransactionBuilder.cpp
    primitives/block.cpp
    primitives/FastBlock.cpp
    primitives/FastTransaction.cpp
    primitives/key.cpp
    primitives/pubkey.cpp
    primitives/pubkey_utils.cpp
    primitives/script.cpp
    primitives/transaction.cpp
    uint256.cpp
    utilmoneystr.cpp
    utilstrencodings.cpp
    utiltime.cpp
    WorkerThreads.cpp
    WaitUntilFinishedHelper.cpp
)

# allow usage in dlls
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

target_link_libraries(flowee_utils secp256k1 univalue flowee_interfaces flowee_crypto)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libflowee_utils.a DESTINATION lib)

install(FILES
    arith_uint256.h
    bloom.h
    hash.h
    version.h
    Logger.h
    tinyformat.h
    WorkerThreads.h
    Message.h
    merkle.h
    PartialMerkleTree.h
    compat.h
    serialize.h
    prevector.h
    uint256.h
    base58.h
    cashaddr.h
DESTINATION include/flowee/utils/)
install(FILES
    streaming/BufferPool.h
    streaming/ConstBuffer.h
    streaming/MessageBuilder.h
    streaming/MessageParser.h
    streaming/P2PParser.h
    streaming/P2PBuilder.h
DESTINATION include/flowee/utils/streaming)

install(FILES
    primitives/FastBlock.h
    primitives/FastTransaction.h
    #primitives/block.h
    primitives/key.h
    primitives/pubkey.h
    primitives/pubkey_utils.h
    primitives/script.h
    #primitives/transaction.h
DESTINATION include/flowee/utils/primitives)

install(FILES support/allocators/secure.h DESTINATION include/flowee/utils/support/allocators)
install(FILES support/pagelocker.h support/cleanse.h DESTINATION include/flowee/utils/support)
