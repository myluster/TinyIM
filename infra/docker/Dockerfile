# 选择基础镜像
FROM ubuntu:22.04

# 避免安装过程中出现交互式提示
ENV DEBIAN_FRONTEND=noninteractive

# 换源 (可选，如果下载慢可以换成阿里云/清华源，这里用官方源)
# RUN sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list

# 安装编译工具和依赖库
# 注意：这里直接安装了开发库 (libxxx-dev)，省去了手动编译 gRPC/Boost 的痛苦
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
    libspdlog-dev \
    libfmt-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /app

# 我们暂时不 COPY 源代码进来

# 默认命令：保持容器运行，方便我们就去编译
CMD ["/bin/bash"]