#include <iostream>
#include <memory>
#include <string>
#include <random>
#include <sstream>
#include <grpcpp/grpcpp.h>
#include "api/v1/auth.grpc.pb.h"
#include "log/logger.hpp"
#include "db/mysql_client.hpp"
#include "db/redis_client.hpp"
#include "config/config.hpp"
#include "utils/password.hpp"
#include "status_client.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using api::v1::AuthService;
using api::v1::LoginReq;
using api::v1::LoginRes;
using api::v1::RegisterReq;
using api::v1::RegisterRes;
using api::v1::VerifyTokenReq;
using api::v1::VerifyTokenRes;
using api::v1::AddFriendReq;
using api::v1::AddFriendRes;
using api::v1::GetFriendListReq;
using api::v1::GetFriendListRes;
using api::v1::HandleFriendRequestReq;
using api::v1::HandleFriendRequestRes;
using api::v1::FriendInfo;
using api::v1::GetPendingFriendRequestsReq;
using api::v1::GetPendingFriendRequestsRes;
using api::v1::DeleteFriendReq;
using api::v1::DeleteFriendRes;

// Global DB Clients (Thread-local or instantiated per request would be better, but for now we use RAII in handlers)
// Actually, MySQLClient and RedisClient are now RAII wrappers around the pool, so we instantiate them in handlers.

std::string GenerateToken() {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    std::string tmp_s;
    tmp_s.reserve(32);

    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, sizeof(alphanum) - 2);

    for (int i = 0; i < 32; ++i) {
        tmp_s += alphanum[dis(gen)];
    }
    return tmp_s;
}

class AuthServiceImpl final : public AuthService::Service {
    std::shared_ptr<StatusClient> status_client_;

public:
    AuthServiceImpl(std::shared_ptr<StatusClient> status_client) : status_client_(status_client) {}

    Status Login(ServerContext* context, const LoginReq* request, LoginRes* reply) override {
        spdlog::info("Login request: {}", request->username());
        
        tinyim::db::MySQLClient mysql;
        tinyim::db::RedisClient redis;

        std::string username = mysql.Escape(request->username());
        std::string query = "SELECT id, password_hash, salt FROM users WHERE username = '" + username + "'";
        // Use Strong Consistency for Login
        auto result = mysql.Query(query, tinyim::db::Consistency::Strong);

        if (result.empty()) {
            reply->set_success(false);
            reply->set_error_msg("User not found");
            return Status::OK;
        }

        std::string db_pass = result[0][1];
        std::string salt = result[0][2];
        int64_t user_id = std::stoll(result[0][0]);

        if (!tinyim::utils::Password::Verify(request->password(), db_pass, salt)) {
            reply->set_success(false);
            reply->set_error_msg("Invalid password");
            return Status::OK;
        }

        std::string token = GenerateToken();
        // Store in Redis: Token -> UserID (Expire in 24h)
        if (redis.SetEx("token:" + token, std::to_string(user_id), 86400)) {
            reply->set_success(true);
            reply->set_user_id(user_id);
            reply->set_token(token);
        } else {
            reply->set_success(false);
            reply->set_error_msg("Internal Redis error");
        }

        return Status::OK;
    }

    Status Register(ServerContext* context, const RegisterReq* request, RegisterRes* reply) override {
        spdlog::info("Register request: {}", request->username());
        
        tinyim::db::MySQLClient mysql;

        std::string username = mysql.Escape(request->username());
        std::string salt = tinyim::utils::Password::GenerateSalt();
        std::string password_hash = tinyim::utils::Password::Hash(request->password(), salt);

        // Check if exists
        auto result = mysql.Query("SELECT id FROM users WHERE username = '" + username + "'");
        if (!result.empty()) {
            reply->set_success(false);
            reply->set_error_msg("Username already exists");
            return Status::OK;
        }

        std::string query = "INSERT INTO users (username, password_hash, salt) VALUES ('" + username + "', '" + password_hash + "', '" + salt + "')";
        if (mysql.Execute(query)) {
            reply->set_success(true);
            reply->set_user_id(mysql.GetLastInsertId());
        } else {
            reply->set_success(false);
            reply->set_error_msg("Database error");
        }
        return Status::OK;
    }

    Status VerifyToken(ServerContext* context, const VerifyTokenReq* request, VerifyTokenRes* reply) override {
        tinyim::db::RedisClient redis;
        auto user_id_str = redis.Get("token:" + request->token());
        if (user_id_str) {
            reply->set_valid(true);
            reply->set_user_id(std::stoll(*user_id_str));
        } else {
            reply->set_valid(false);
        }
        return Status::OK;
    }

    Status AddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRes* reply) override {
        tinyim::db::MySQLClient mysql;
        int64_t sender_id = request->user_id();
        int64_t receiver_id = request->friend_id();
        
        spdlog::info("AddFriend request: sender_id={}, receiver_id={}", sender_id, receiver_id);
        
        if (sender_id == receiver_id) {
            reply->set_success(false);
            reply->set_error_msg("Cannot add yourself");
            return Status::OK;
        }

        // Check if user exists (Use Master to avoid "User not found" for new users)
        std::string check_user = "SELECT 1 FROM users WHERE id = " + std::to_string(receiver_id);
        auto user_res = mysql.Query(check_user, tinyim::db::Consistency::Strong);
        if (user_res.empty()) {
            spdlog::warn("User {} not found (Query returned empty)", receiver_id);
            reply->set_success(false);
            reply->set_error_msg("User not found");
            return Status::OK;
        }

        // Check if already friends
        std::string check_friend = "SELECT 1 FROM friends WHERE user_id = " + std::to_string(sender_id) + " AND friend_id = " + std::to_string(receiver_id);
        if (!mysql.Query(check_friend).empty()) {
            reply->set_success(false);
            reply->set_error_msg("Already friends");
            return Status::OK;
        }

        // Check if request already pending
        std::string check_req = "SELECT 1 FROM friend_requests WHERE sender_id = " + std::to_string(sender_id) + " AND receiver_id = " + std::to_string(receiver_id) + " AND status = 0";
        if (!mysql.Query(check_req).empty()) {
            reply->set_success(false);
            reply->set_error_msg("Request already pending");
            return Status::OK;
        }

        std::string query = "INSERT INTO friend_requests (sender_id, receiver_id, status) VALUES (" + std::to_string(sender_id) + ", " + std::to_string(receiver_id) + ", 0)";
        if (mysql.Execute(query)) {
            reply->set_success(true);
        } else {
            reply->set_success(false);
            reply->set_error_msg("Database error");
        }
        return Status::OK;
    }

    Status GetFriendList(ServerContext* context, const GetFriendListReq* request, GetFriendListRes* reply) override {
        tinyim::db::MySQLClient mysql;
        int64_t user_id = request->user_id();
        std::string query = "SELECT u.id, u.username FROM friends f JOIN users u ON f.friend_id = u.id WHERE f.user_id = " + std::to_string(user_id);
        auto result = mysql.Query(query, tinyim::db::Consistency::Strong);

        std::vector<int64_t> friend_ids;
        for (const auto& row : result) {
            friend_ids.push_back(std::stoll(row[0]));
        }

        // Query Status Server
        std::map<int64_t, int> status_map;
        if (!friend_ids.empty()) {
            status_map = status_client_->GetStatus(friend_ids);
        }

        reply->set_success(true);
        for (const auto& row : result) {
            auto* friend_info = reply->add_friends();
            int64_t fid = std::stoll(row[0]);
            friend_info->set_user_id(fid);
            friend_info->set_username(row[1]);
            
            // Set status from map (default 0 if not found)
            friend_info->set_status(status_map[fid]); 
        }
        return Status::OK;
    }

    Status HandleFriendRequest(ServerContext* context, const HandleFriendRequestReq* request, HandleFriendRequestRes* reply) override {
        tinyim::db::MySQLClient mysql;
        int64_t user_id = request->user_id(); // Receiver
        int64_t sender_id = request->request_id(); // Sender ID (treated as request_id for now)
        bool accept = request->accept();

        spdlog::info("HandleFriendRequest: user_id={}, sender_id={}, accept={}", user_id, sender_id, accept);

        // Find pending request (Use Strong Consistency)
        std::string find_req = "SELECT id FROM friend_requests WHERE sender_id = " + std::to_string(sender_id) + " AND receiver_id = " + std::to_string(user_id) + " AND status = 0";
        auto result = mysql.Query(find_req, tinyim::db::Consistency::Strong);
        if (result.empty()) {
            reply->set_success(false);
            reply->set_error_msg("Request not found");
            return Status::OK;
        }
        std::string req_db_id = result[0][0];

        int status = accept ? 1 : 2;
        std::string update_req = "UPDATE friend_requests SET status = " + std::to_string(status) + " WHERE id = " + req_db_id;
        mysql.Execute(update_req);

        if (accept) {
            // Insert into friends (bidirectional)
            std::string insert_f1 = "INSERT INTO friends (user_id, friend_id) VALUES (" + std::to_string(user_id) + ", " + std::to_string(sender_id) + ")";
            std::string insert_f2 = "INSERT INTO friends (user_id, friend_id) VALUES (" + std::to_string(sender_id) + ", " + std::to_string(user_id) + ")";
            mysql.Execute(insert_f1);
            mysql.Execute(insert_f2);
        }

        reply->set_success(true);
        return Status::OK;
    }

    Status GetPendingFriendRequests(ServerContext* context, const GetPendingFriendRequestsReq* request, GetPendingFriendRequestsRes* reply) override {
        tinyim::db::MySQLClient mysql;
        int64_t user_id = request->user_id();
        // Query friend_requests joined with users to get sender info
        std::string query = "SELECT fr.id, fr.sender_id, u.username, UNIX_TIMESTAMP(fr.created_at) "
                            "FROM friend_requests fr "
                            "JOIN users u ON fr.sender_id = u.id "
                            "WHERE fr.receiver_id = " + std::to_string(user_id) + " AND fr.status = 0";
        
        auto result = mysql.Query(query, tinyim::db::Consistency::Strong);

        reply->set_success(true);
        for (const auto& row : result) {
            auto* req = reply->add_requests();
            req->set_request_id(std::stoll(row[0]));
            req->set_sender_id(std::stoll(row[1]));
            req->set_sender_username(row[2]);
            if (!row[3].empty()) {
                req->set_created_at(std::stoll(row[3]));
            } else {
                req->set_created_at(0);
            }
        }
        return Status::OK;
    }


    Status DeleteFriend(ServerContext* context, const DeleteFriendReq* request, DeleteFriendRes* reply) override {
        tinyim::db::MySQLClient mysql;
        int64_t user_id = request->user_id();
        int64_t friend_id = request->friend_id();
        spdlog::info("DeleteFriend request: user_id={}, friend_id={}", user_id, friend_id);

        // Delete from friends table (Bidirectional)
        std::string delete_f1 = "DELETE FROM friends WHERE user_id = " + std::to_string(user_id) + " AND friend_id = " + std::to_string(friend_id);
        std::string delete_f2 = "DELETE FROM friends WHERE user_id = " + std::to_string(friend_id) + " AND friend_id = " + std::to_string(user_id);
        
        // Also delete friend requests? Maybe not strictly necessary but cleaner.
        std::string delete_req1 = "DELETE FROM friend_requests WHERE sender_id = " + std::to_string(user_id) + " AND receiver_id = " + std::to_string(friend_id);
        std::string delete_req2 = "DELETE FROM friend_requests WHERE sender_id = " + std::to_string(friend_id) + " AND receiver_id = " + std::to_string(user_id);

        if (mysql.Execute(delete_f1) && mysql.Execute(delete_f2)) {
            mysql.Execute(delete_req1);
            mysql.Execute(delete_req2);
            reply->set_success(true);
        } else {
            reply->set_success(false);
            reply->set_error_msg("Database error");
        }
        return Status::OK;
    }
};

void RunServer() {
    auto& config = tinyim::Config::Instance();
    std::string server_address("0.0.0.0:" + std::to_string(config.Server().auth_port));
    
    std::string status_address = config.Services().status_address;
    auto status_client = std::make_shared<StatusClient>(grpc::CreateChannel(status_address, grpc::InsecureChannelCredentials()));

    AuthServiceImpl service(status_client);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    spdlog::info("Auth Server listening on {}", server_address);
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
