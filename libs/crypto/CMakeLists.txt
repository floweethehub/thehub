include_directories(${LIBCRYPTO_INCLUDES})

add_library(flowee_crypto STATIC
    aes.cpp
    hmac_sha256.cpp
    hmac_sha512.cpp
    ripemd160.cpp
    sha1.cpp
    sha256.cpp
    sha512.cpp
)
