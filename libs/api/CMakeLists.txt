include_directories(${LIBAPI_INCLUDES})

add_library(flowee_api STATIC
    AdminRPCBinding.cpp
    AdminServer.cpp
)

target_link_libraries(flowee_api flowee_server flowee_networkmanager)
