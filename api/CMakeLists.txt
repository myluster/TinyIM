file(GLOB_RECURSE PROTO_FILES "v1/*.proto")

find_program(gRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

include_directories(${PROTO_GEN_DIR})

foreach(proto_file ${PROTO_FILES})
    get_filename_component(FIL_WE ${proto_file} NAME_WE)
    
    set(PB_H "${PROTO_GEN_DIR}/api/v1/${FIL_WE}.pb.h")
    set(PB_CC "${PROTO_GEN_DIR}/api/v1/${FIL_WE}.pb.cc")
    set(GRPC_H "${PROTO_GEN_DIR}/api/v1/${FIL_WE}.grpc.pb.h")
    set(GRPC_CC "${PROTO_GEN_DIR}/api/v1/${FIL_WE}.grpc.pb.cc")

    add_custom_command(
        OUTPUT ${PB_H} ${PB_CC} ${GRPC_H} ${GRPC_CC}
        COMMAND protobuf::protoc
        ARGS --cpp_out=${PROTO_GEN_DIR}
             --grpc_out=${PROTO_GEN_DIR}
             --plugin=protoc-gen-grpc=${gRPC_CPP_PLUGIN_EXECUTABLE}
             -I ${CMAKE_SOURCE_DIR}  # 使用项目根目录作为 include root
             ${proto_file}
        DEPENDS ${proto_file}
        COMMENT "Compiling proto: ${proto_file}"
    )

    list(APPEND PROTO_SRCS ${PB_H} ${PB_CC} ${GRPC_H} ${GRPC_CC})
endforeach()

# 将生成的代码打包成一个静态库，名字叫 tinyim_proto
add_library(tinyim_proto STATIC ${PROTO_SRCS})

target_link_libraries(tinyim_proto 
    PUBLIC 
    protobuf::libprotobuf 
    gRPC::grpc++
)

target_include_directories(tinyim_proto 
    PUBLIC ${PROTO_GEN_DIR}
)