include_directories(${LIBUTILS_INCLUDES})

add_library(flowee_utils STATIC
    amount.cpp
    allowed_args.cpp
    chainparamsbase.cpp
    clientversion.cpp
    compat/glibc_sanity.cpp
    compat/glibcxx_sanity.cpp
    compat/strnlen.cpp
    LogChannels.cpp
    Logger.cpp
    Message.cpp
    random.cpp
    rpcprotocol.cpp
    streaming/BufferPool.cpp
    streaming/ConstBuffer.cpp
    streaming/MessageBuilder.cpp
    streaming/MessageParser.cpp
    support/cleanse.cpp
    support/pagelocker.cpp
    sync.cpp
    uint256.cpp
    util.cpp
    utilmoneystr.cpp
    utilstrencodings.cpp
    utiltime.cpp
)

add_dependencies(flowee_utils univalue)

target_link_libraries(flowee_utils flowee_crypto)
