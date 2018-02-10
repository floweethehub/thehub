include_directories(${FL_LIBWALLET_INCLUDES})

add_library(flowee_wallet STATIC
    crypter.cpp
    db.cpp
    rpcdump.cpp
    rpcwallet.cpp
    wallet.cpp
    walletdb.cpp
    wallet_ismine.cpp
)

target_link_libraries(flowee_wallet flowee_server)
