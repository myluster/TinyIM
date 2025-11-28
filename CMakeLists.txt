cmake_minimum_required(VERSION 3.20)

# 定义项目名称和版本
project(TinyIM VERSION 0.1.0 LANGUAGES CXX)

# 强制使用 C++20 标准 (协程等特性必须)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 生成 compile_commands.json (方便 VSCode 代码提示)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- 2. 依赖管理 ---
find_package(Protobuf REQUIRED)
# gRPC 查找逻辑：优先 CONFIG 模式，失败则尝试 pkg-config
find_package(gRPC CONFIG)
if (NOT gRPC_FOUND)
    message(STATUS "gRPC CONFIG not found, trying pkg-config")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(gRPC REQUIRED grpc++ grpc)
    
    # 如果 pkg-config 找到了，手动创建 gRPC::grpc++ 目标
    if (gRPC_FOUND AND NOT TARGET gRPC::grpc++)
        add_library(gRPC::grpc++ INTERFACE IMPORTED)
        target_include_directories(gRPC::grpc++ INTERFACE ${gRPC_INCLUDE_DIRS})
        target_link_libraries(gRPC::grpc++ INTERFACE ${gRPC_LIBRARIES})
        message(STATUS "Created gRPC::grpc++ target from pkg-config")
    endif()
endif()
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

# 先编译 api 生成代码，后续的服务才能引用
add_subdirectory(api)

# 暂时先注释掉服务层，等 api 编译通了再打开
# add_subdirectory(services/gateway)
# add_subdirectory(services/auth)
# add_subdirectory(services/chat)