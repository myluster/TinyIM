#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <grpcpp/grpcpp.h>
#include "api/v1/chat.grpc.pb.h"
#include "log/logger.hpp"
#include "db/mysql_client.hpp"
#include "db/redis_client.hpp"
#include "config/config.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using api::v1::ChatService;
using api::v1::ChatPacket;
using api::v1::SaveMessageRes;
using api::v1::GetHistoryReq;
using api::v1::GetHistoryRes;
using api::v1::GetRecentSessionsReq;
using api::v1::GetRecentSessionsRes;
using api::v1::GetOfflineMessagesReq;
using api::v1::GetOfflineMessagesRes;
using api::v1::Session;

class ChatServiceImpl final : public ChatService::Service {
    Status SaveMessage(ServerContext* context, const ChatPacket* request, SaveMessageRes* reply) override {
        spdlog::info("SaveMessage request from user: {} to user: {}", request->from_user_id(), request->to_user_id());
        
        tinyim::db::MySQLClient mysql;
        std::string content = mysql.Escape(request->content());
        int64_t timestamp = request->timestamp();
        
        // 1. Insert into messages
        std::string query_msg = "INSERT INTO messages (from_id, to_id, content, created_at) VALUES (" + 
                            std::to_string(request->from_user_id()) + ", " + 
                            std::to_string(request->to_user_id()) + ", '" + 
                            content + "', FROM_UNIXTIME(" + std::to_string(timestamp / 1000) + "))";
        
        if (!mysql.Execute(query_msg)) {
            reply->set_success(false);
            reply->set_error_msg("Database error: Save Message");
            return Status::OK;
        }
        
        int64_t msg_id = mysql.GetLastInsertId();
        reply->set_msg_id(msg_id);

        // 2. Upsert into sessions (Bidirectional)
        // For Sender:
        UpsertSession(mysql, request->from_user_id(), request->to_user_id(), content, timestamp, false);
        // For Receiver:
        UpsertSession(mysql, request->to_user_id(), request->from_user_id(), content, timestamp, true);

        reply->set_success(true);
        return Status::OK;
    }

    Status GetHistory(ServerContext* context, const GetHistoryReq* request, GetHistoryRes* reply) override {
        spdlog::info("GetHistory request for user: {} with peer: {}", request->user_id(), request->peer_id());
        
        tinyim::db::MySQLClient mysql;
        std::string u1 = std::to_string(request->user_id());
        std::string u2 = std::to_string(request->peer_id());
        
        std::string query = "SELECT id, from_id, to_id, content, UNIX_TIMESTAMP(created_at) * 1000 FROM messages WHERE "
                            "(from_id=" + u1 + " AND to_id=" + u2 + ") OR "
                            "(from_id=" + u2 + " AND to_id=" + u1 + ") "
                            "ORDER BY created_at ASC LIMIT " + std::to_string(request->limit());
                            
        auto result = mysql.Query(query);
        for (const auto& row : result) {
            auto* msg = reply->add_messages();
            msg->set_msg_id(std::stoll(row[0]));
            msg->set_from_user_id(std::stoll(row[1]));
            msg->set_to_user_id(std::stoll(row[2]));
            msg->set_content(row[3]);
            msg->set_timestamp(std::stoll(row[4]));
        }
        
        return Status::OK;
    }

    Status GetRecentSessions(ServerContext* context, const GetRecentSessionsReq* request, GetRecentSessionsRes* reply) override {
        int64_t user_id = request->user_id();
        spdlog::info("GetRecentSessions request for user: {}", user_id);

        tinyim::db::MySQLClient mysql;
        // Query sessions table (Use Strong Consistency)
        std::string query = "SELECT peer_id, last_msg_content, last_msg_timestamp, unread_count FROM sessions WHERE user_id = " + 
                            std::to_string(user_id) + " ORDER BY last_msg_timestamp DESC";

        auto result = mysql.Query(query, tinyim::db::Consistency::Strong);
        
        for (const auto& row : result) {
            auto* session = reply->add_sessions();
            session->set_peer_id(std::stoll(row[0]));
            session->set_last_msg_content(row[1]);
            session->set_last_msg_timestamp(std::stoll(row[2]));
            session->set_unread_count(std::stoi(row[3]));
        }

        return Status::OK;
    }

    Status GetOfflineMessages(ServerContext* context, const GetOfflineMessagesReq* request, GetOfflineMessagesRes* reply) override {
        int64_t user_id = request->user_id();
        spdlog::info("GetOfflineMessages request for user: {}", user_id);

        tinyim::db::MySQLClient mysql;
        
        // 1. Find sessions with unread messages (Use Strong Consistency)
        std::string session_query = "SELECT peer_id, unread_count FROM sessions WHERE user_id = " + std::to_string(user_id) + " AND unread_count > 0";
        auto sessions = mysql.Query(session_query, tinyim::db::Consistency::Strong);

        for (const auto& row : sessions) {
            int64_t peer_id = std::stoll(row[0]);
            int unread_count = std::stoi(row[1]);
            
            // 2. Fetch last N messages for this session
            // Note: This logic assumes unread messages are the latest ones.
            // We fetch the latest 'unread_count' messages.
            std::string u1 = std::to_string(user_id);
            std::string u2 = std::to_string(peer_id);
            
            std::string msg_query = "SELECT id, from_id, to_id, content, UNIX_TIMESTAMP(created_at) * 1000 FROM messages WHERE "
                                    "(from_id=" + u1 + " AND to_id=" + u2 + ") OR "
                                    "(from_id=" + u2 + " AND to_id=" + u1 + ") "
                                    "ORDER BY created_at DESC LIMIT " + std::to_string(unread_count);
            
            auto messages = mysql.Query(msg_query, tinyim::db::Consistency::Strong);
            
            // Messages are retrieved in reverse chronological order (DESC), we need to add them.
            // But usually client expects them in order? Or just a list.
            // Let's add them. Client can sort if needed, or we can reverse.
            // Since we are pushing "offline messages", order matters.
            // Let's reverse them to be chronological.
            for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
                const auto& msg_row = *it;
                auto* msg = reply->add_messages();
                msg->set_msg_id(std::stoll(msg_row[0]));
                msg->set_from_user_id(std::stoll(msg_row[1]));
                msg->set_to_user_id(std::stoll(msg_row[2]));
                msg->set_content(msg_row[3]);
                msg->set_timestamp(std::stoll(msg_row[4]));
            }
        }
        
        // 3. Reset unread count? 
        // Usually fetching offline messages implies they are delivered.
        // But maybe we should let client acknowledge? 
        // For simplicity in this "push" model, we can reset unread count here or assume client will read them.
        // However, if we just push, we haven't received ACK.
        // But the requirement is "GetOfflineMessages".
        // Let's NOT reset unread count here. The client should send "Read" receipt or we rely on "GetRecentSessions" to show unread count.
        // Wait, if we push them, they are "delivered".
        // If we don't reset, next time we connect we get them again? Yes, which is good for reliability until ACK.
        // But for this task, let's just fetch.
        
        return Status::OK;
    }

private:
    void UpsertSession(tinyim::db::MySQLClient& mysql, int64_t user_id, int64_t peer_id, const std::string& content, int64_t timestamp, bool inc_unread) {
        // ON DUPLICATE KEY UPDATE
        std::string unread_update = inc_unread ? "unread_count = unread_count + 1" : "unread_count = unread_count";
        // If I am sender, unread count for me doesn't change (or resets? usually resets if I send). 
        // Let's assume if I send, my unread count for that session is 0? No, maybe I just replied.
        // Usually if I send, I've read everything. So unread_count = 0.
        if (!inc_unread) unread_update = "unread_count = 0";

        std::string query = "INSERT INTO sessions (user_id, peer_id, last_msg_content, last_msg_timestamp, unread_count) VALUES (" +
                            std::to_string(user_id) + ", " + std::to_string(peer_id) + ", '" + content + "', " + std::to_string(timestamp) + ", " + (inc_unread ? "1" : "0") + ") " +
                            "ON DUPLICATE KEY UPDATE last_msg_content = '" + content + "', last_msg_timestamp = " + std::to_string(timestamp) + ", " + unread_update;
        
        mysql.Execute(query);
    }
};

void RunServer() {
    auto& config = tinyim::Config::Instance();
    std::string server_address("0.0.0.0:" + std::to_string(config.Server().chat_port));
    ChatServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    spdlog::info("Chat Server listening on {}", server_address);
    server->Wait();
}

int main(int argc, char** argv) {
    tinyim::Logger::Init();
    
    // Load Config
    std::string config_path = "configs/config.json";
    if (argc > 1) {
        config_path = argv[1];
    }

    if (!tinyim::Config::Instance().Load(config_path)) {
        spdlog::error("Failed to load config from {}", config_path);
        return 1;
    }

    // Init DB Pools
    tinyim::db::MySQLPool::Instance().Init(tinyim::Config::Instance().MySQL(), tinyim::Config::Instance().MySQLReadOnly());
    tinyim::db::RedisPool::Instance().Init(tinyim::Config::Instance().Redis(), tinyim::Config::Instance().RedisSentinel());

    RunServer();
    return 0;
}
