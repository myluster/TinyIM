project(tinyim_tests)

# Functional Tests
add_executable(functional_tests functional/main.cpp)
target_link_libraries(functional_tests
    PRIVATE
    tinyim_common
    tinyim_proto
    gRPC::grpc++
    protobuf::libprotobuf
    Boost::system
    Boost::thread
    OpenSSL::SSL
    OpenSSL::Crypto
)
target_include_directories(functional_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/services/gateway
    ${CMAKE_SOURCE_DIR}/services/common
    ${CMAKE_SOURCE_DIR}/api
)

# Status Tests
add_executable(status_tests functional/test_status.cpp)
target_link_libraries(status_tests
    PRIVATE
    tinyim_common
    tinyim_proto
    gRPC::grpc++
    protobuf::libprotobuf
    Boost::system
    Boost::thread
    OpenSSL::SSL
    OpenSSL::Crypto
)
target_include_directories(status_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/services/gateway
    ${CMAKE_SOURCE_DIR}/services/common
    ${CMAKE_SOURCE_DIR}/api
)

# Offline Msgs Tests
add_executable(offline_msgs_tests functional/test_offline_msgs.cpp)
target_link_libraries(offline_msgs_tests
    PRIVATE
    tinyim_common
    tinyim_proto
    gRPC::grpc++
    protobuf::libprotobuf
    Boost::system
    Boost::thread
    OpenSSL::SSL
    OpenSSL::Crypto
)
target_include_directories(offline_msgs_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/services/gateway
    ${CMAKE_SOURCE_DIR}/services/common
    ${CMAKE_SOURCE_DIR}/api
)

# Stress Tests
add_executable(stress_tests stress/load_generator.cpp)
target_link_libraries(stress_tests
    PRIVATE
    tinyim_common
    tinyim_proto
    gRPC::grpc++
    protobuf::libprotobuf
    Boost::system
    Boost::thread
    OpenSSL::SSL
    OpenSSL::Crypto
)
target_include_directories(stress_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/services/gateway
    ${CMAKE_SOURCE_DIR}/services/common
    ${CMAKE_SOURCE_DIR}/api
)

# Features Tests (AckMessages, DeleteFriend)
add_executable(test_features functional/test_features.cpp)
target_link_libraries(test_features
    PRIVATE
    tinyim_common
    tinyim_proto
    gRPC::grpc++
    protobuf::libprotobuf
    Boost::system
    Boost::thread
    OpenSSL::SSL
    OpenSSL::Crypto
)
target_include_directories(test_features PRIVATE
    ${CMAKE_SOURCE_DIR}/services/gateway
    ${CMAKE_SOURCE_DIR}/services/common
    ${CMAKE_SOURCE_DIR}/api
)
