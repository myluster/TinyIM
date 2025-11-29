# 选择基础镜像
FROM ubuntu:22.04

# 避免安装过程中出现交互式提示
ENV DEBIAN_FRONTEND=noninteractive

# 安装编译工具和依赖库
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    ninja-build \
    git \
    gdb \
    libssl-dev \
    libboost-all-dev \
    libprotobuf-dev \
    protobuf-compiler \
    libgrpc++-dev \
    protobuf-compiler-grpc \
    libmysqlclient-dev \
    mysql-client \
    libhiredis-dev \
    redis-tools \
    libspdlog-dev \
    libfmt-dev && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /app

# 默认命令：保持容器运行，方便我们进入编译
CMD ["/bin/bash"]