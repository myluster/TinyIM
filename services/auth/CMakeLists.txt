project(tinyim_auth)

add_executable(auth_server
    main.cpp
)

target_link_libraries(auth_server
    PRIVATE
    tinyim_proto
    tinyim_common
    gRPC::grpc++
    protobuf::libprotobuf
)
