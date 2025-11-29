#pragma once
#include <mutex>
#include <unordered_map>
#include <memory>
#include <string>
#include "log/logger.hpp"
#include "api/v1/gateway.pb.h"

// Forward declaration
class websocket_session;

// SessionManager: 管理所有在线用户的 WebSocket 会话
class SessionManager {
    std::mutex mutex_;
    std::unordered_map<int64_t, websocket_session*> sessions_; // UserID -> Session 映射
    std::string gateway_id_;

public:
    SessionManager(const std::string& gateway_id) : gateway_id_(gateway_id) {}

    // 用户上线，注册会话
    void join(int64_t user_id, websocket_session* session);

    // 用户下线，移除会话
    void leave(int64_t user_id);

    // 发送消息给指定用户（如果在线）
    void send_to_user(int64_t user_id, const api::v1::GatewayMessage& message);
    
    // 仅发送给本地用户 (由 Redis Pub/Sub 回调触发)
    void send_to_local_user(int64_t user_id, const api::v1::GatewayMessage& message);
};
