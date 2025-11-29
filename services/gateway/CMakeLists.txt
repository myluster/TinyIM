project(tinyim_gateway)

find_package(PkgConfig REQUIRED)


add_executable(gateway_server
    main.cpp
)

target_link_libraries(gateway_server
    PRIVATE
    tinyim_proto
    tinyim_common
    gRPC::grpc++
    protobuf::libprotobuf
    Boost::system
    Boost::thread
)
