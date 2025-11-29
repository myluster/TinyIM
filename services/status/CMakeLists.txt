add_executable(status_server main.cpp)

target_link_libraries(status_server
    PRIVATE
    tinyim_common
    tinyim_proto
    gRPC::grpc++
    protobuf::libprotobuf
    hiredis
    spdlog::spdlog
    fmt::fmt
)
