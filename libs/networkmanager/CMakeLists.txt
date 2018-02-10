include_directories(${LIBNETWORKMANAGER_INCLUDES})

add_library(flowee_networkmanager STATIC
    NetworkConnection.cpp
    NetworkManager.cpp
)

target_link_libraries(flowee_networkmanager flowee_utils)
