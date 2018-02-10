include_directories(${LIBCONSOLE_INCLUDES})

add_library(flowee_console STATIC
    rpcclient.cpp
)

add_dependencies(flowee_console univalue)
