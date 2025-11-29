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

# gRPC 查找：手动查找库文件和创建目标
find_library(GRPC_LIBRARY NAMES grpc++ REQUIRED)
find_library(GRPC_LIBRARY_CORE NAMES grpc REQUIRED)
find_library(GPR_LIBRARY NAMES gpr REQUIRED)

# 手动创建 gRPC::grpc++ 目标
add_library(gRPC::grpc++ INTERFACE IMPORTED)
target_link_libraries(gRPC::grpc++ INTERFACE ${GRPC_LIBRARY} ${GRPC_LIBRARY_CORE} ${GPR_LIBRARY})

message(STATUS "Found gRPC libraries:")
message(STATUS "  grpc++: ${GRPC_LIBRARY}")
message(STATUS "  grpc: ${GRPC_LIBRARY_CORE}")
message(STATUS "  gpr: ${GPR_LIBRARY}")

find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

# 先编译 api 生成代码，后续的服务才能引用
add_subdirectory(api)

add_subdirectory(services/common)
add_subdirectory(services/auth)
add_subdirectory(services/chat)
add_subdirectory(services/gateway)
add_subdirectory(services/status)
add_subdirectory(tests)