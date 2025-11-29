# TinyIM - Modern C++ IM Learning Project

## 1. 项目简介
TinyIM 是一个旨在探索现代即时通讯（IM）架构的学习型项目。
本项目的核心目标不是复刻一个功能大而全的微信或 QQ，而是通过实现一个**最小可行性（MVP）的 IM 系统**，深入理解端到端的消息流转、现代 C++ (C++20) 的工程实践、微服务架构的设计模式以及跨平台开发流程。

### 核心目标
* **架构优先**：使用微服务架构，实践 gRPC 服务间通信。
* **高性能**：后端利用 C++20 协程、Boost.Asio 和 Boost.Beast 实现高并发处理。
* **全链路**：打通从 Windows Qt 客户端 / Web 客户端到 Linux 后端服务的完整通信链路。

## 2. 技术栈

### 客户端
* **Desktop (Windows):**
    * 框架: Qt 6 (C++)
    * 构建: CMake, MSVC
    * 网络: QNetworkAccessManager (HTTP), QWebSocket (WebSocket)
* **Web:**
    * 框架: React
    * 网络: Native WebSocket API

### 服务端
* **开发环境:** WSL 2 (Ubuntu 22.04), VS Code Remote
* **语言标准:** C++ 17/20
* **核心库:**
    * **Boost.Asio:** 异步网络编程
    * **Boost.Beast:** HTTP 和 WebSocket 协议实现
    * **gRPC / Protobuf:** 微服务间的高性能 RPC 通信
* **构建系统:** CMake, Ninja

### 基础设施
* **容器化:** Docker, Docker Compose
* **数据库:** MySQL 8.0+ (用户数据、消息持久化)
* **中间件:** Redis 6.0+ (Session 缓存、消息发布/订阅)

## 3. 功能规划

为了聚焦架构复杂度，本项目仅实现以下核心功能：

1.  **用户认证体系**
    * 账号注册与登录 (Token Based)。
    * 网关鉴权。
2.  **连接保活 (Heartbeat)**
    * **应用层心跳**: 定时发送 Ping/Pong 包，检测半开连接。
    * **断线重连**: 客户端网络切换或服务重启时的自动恢复机制。
3.  **消息网关 (Gateway)**
    * 维护客户端 WebSocket 长连接。
    * 作为流量入口，分发请求至后端微服务。
4.  **即时通讯 (Chat)**
    * 点对点 (1-on-1) 纯文本聊天。
    * 用户在线状态感知。
    * 多端消息同步 (Web 端发送，Qt 端接收)。
5.  **好友管理 (Friend Management)**
    *   **添加好友**: 支持通过 ID 或用户名搜索并添加好友。
    *   **好友列表**: 展示好友列表及在线状态。
    *   **好友申请**: 处理好友请求（接受/拒绝）。
6.  **会话管理 (Session Management)**
    *   **最近会话**: 维护最近联系人列表，按时间排序。
    *   **未读计数**: 统计每个会话的未读消息数。
7.  **文件传输 (File Transfer)**
    * **策略**: HTTP 控制流与数据流分离。
    * **断点下载**: 支持 HTTP `Range` 头，允许暂停和恢复下载。
    *   **分片上传**: 简单的基于 Offset 的分块上传机制，支持网络中断后续传。
    *   **服务端实现**: 使用 Boost.Beast 处理文件 IO 和 HTTP Range 请求。
8.  **客户端本地存储 (Local Cache - Qt)**
    *   **消息漫游与缓存**: 使用 **SQLite** 本地存储聊天记录，优先展示本地历史，减少服务器拉取频率。
    *   **文件管理**: 维护本地文件索引，避免重复下载已存在的文件。

## 4. 项目结构与模块说明

本项目采用单体仓库模式管理，目录结构如下：

```text
TinyIM/
├── api/                 # [核心] Protobuf 定义
│   └── v1/              # 协议版本控制
│
├── configs/             # [配置] 环境配置文件模版
│   ├── gateway.yaml     # 网关配置 (端口, SSL路径, 超时设置)
│   ├── auth.yaml        # 认证服务配置 (DB连接串, Token秘钥)
│   └── chat.yaml        # 聊天服务配置 (Redis地址, 存储策略)
│
├── cmake/               # [构建] CMake 辅助脚本/模块
│
├── clients/             # [前端] 客户端集合
│   ├── qt/              # Windows Desktop (C++/Qt)
│   └── web/             # Web Browser (Vue/React)
│
├── services/            # [后端] 微服务集合 (Linux/WSL)
│   ├── common/          # [新增] 通用组件库 (Shared Lib)
│   │   ├── config/      # 配置加载模块 (解析 YAML -> C++ Struct)
│   │   ├── log/         # 统一日志模块
│   │   └── db/          # 数据库/Redis 连接池封装
│   │
│   ├── gateway/         # 接入网关
│   ├── auth/            # 认证服务
│   └── chat/            # 聊天服务
│
├── infra/               # [运维] 基础设施代码
│   ├── docker/          # Dockerfiles
│   ├── compose/         # docker-compose.yml
│   └── scripts/         # 启动脚本
│
├── third_party/         # [依赖] 外部库源码
├── CMakeLists.txt       # 根构建脚本
└── README.md
```

## 常用脚本 (位于 `infra/scripts/`)

为了方便开发，我们提供了一系列 Windows 批处理脚本：

*   **启动环境**: `start_dev.bat` (启动 Docker 容器)
*   **停止环境**: `stop_dev.bat` (停止 Docker 容器)
*   **重置数据库**: `reset_db.bat` (清空数据库数据)
*   **编译后端**: `build_backend.bat` (在容器内执行 make)
*   **运行服务**: `run_backend.bat` (在容器内启动服务)
*   **查看日志**: `monitor_logs.bat` (实时查看 auth, chat, gateway 日志)
*   **进入 Shell**: `enter_dev.bat` (进入容器命令行)

### 推荐开发流程
1. `start_dev.bat`
2. `build_backend.bat`
3. `run_backend.bat`
4. `monitor_logs.bat` (新开一个终端窗口保持运行)

## 手动验证步骤
(请参考 `verification_guide.md`)