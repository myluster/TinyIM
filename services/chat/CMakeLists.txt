project(tinyim_chat)

add_executable(chat_server
    main.cpp
)

target_link_libraries(chat_server
    PRIVATE
    tinyim_proto
    tinyim_common
    gRPC::grpc++
    protobuf::libprotobuf
)
