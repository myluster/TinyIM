project(tinyim_common)

find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(tinyim_common STATIC
    log/logger.hpp
    log/logger.cpp
)

target_link_libraries(tinyim_common
    PUBLIC
    spdlog::spdlog
    fmt::fmt
    mysqlclient
    hiredis
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_include_directories(tinyim_common
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
