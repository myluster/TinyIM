#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <grpcpp/grpcpp.h>
#include "api/v1/status.grpc.pb.h"
#include "api/v1/auth.grpc.pb.h"
#include "api/v1/gateway.pb.h"
#include "log/logger.hpp"
#include "db/redis_client.hpp"
#include "config/config.hpp"
// #include "auth_client.hpp" // Removed: StatusAuthClient is defined locally

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using api::v1::StatusService;
using api::v1::LoginStatusReq;
using api::v1::LoginStatusRes;
using api::v1::LogoutStatusReq;
using api::v1::LogoutStatusRes;
using api::v1::GetStatusReq;
using api::v1::GetStatusRes;

// Simple AuthClient wrapper for Status Server (similar to Gateway's but simplified)
class StatusAuthClient {
public:
    StatusAuthClient(std::shared_ptr<grpc::Channel> channel)
        : stub_(api::v1::AuthService::NewStub(channel)) {}

    std::vector<int64_t> GetFriendIds(int64_t user_id) {
        api::v1::GetFriendListReq request;
        request.set_user_id(user_id);
        api::v1::GetFriendListRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->GetFriendList(&context, request, &reply);
        
        std::vector<int64_t> friend_ids;
        if (status.ok() && reply.success()) {
            for (const auto& f : reply.friends()) {
                friend_ids.push_back(f.user_id());
            }
        } else {
            spdlog::error("Failed to get friend list for user {}", user_id);
        }
        return friend_ids;
    }

private:
    std::unique_ptr<api::v1::AuthService::Stub> stub_;
};

class StatusServiceImpl final : public StatusService::Service {
    std::shared_ptr<StatusAuthClient> auth_client_;

public:
    StatusServiceImpl(std::shared_ptr<StatusAuthClient> auth_client) : auth_client_(auth_client) {}

    Status Login(ServerContext* context, const LoginStatusReq* request, LoginStatusRes* reply) override {
        int64_t user_id = request->user_id();
        spdlog::info("User {} Login Status", user_id);

        tinyim::db::RedisClient redis;
        // 1. Set Status = 1 (Online)
        redis.Set("user:status:" + std::to_string(user_id), "1");
        
        // 2. Get Friends
        auto friend_ids = auth_client_->GetFriendIds(user_id);
        spdlog::info("User {} has {} friends", user_id, friend_ids.size());

        // 3. Check Friends Status and Notify them
        reply->set_success(true);
        for (int64_t fid : friend_ids) {
            auto status_opt = redis.Get("user:status:" + std::to_string(fid));
            std::string status_val = status_opt ? *status_opt : "null";
            spdlog::info("Checking friend {}: status={}", fid, status_val);
            
            if (status_opt && *status_opt == "1") {
                reply->add_online_friend_ids(fid);
                
                // Notify friend that I am online
                spdlog::info("Notifying friend {} that user {} is online", fid, user_id);
                NotifyUser(fid, user_id, 1);
            }
        }

        return Status::OK;
    }

    Status Logout(ServerContext* context, const LogoutStatusReq* request, LogoutStatusRes* reply) override {
        int64_t user_id = request->user_id();
        spdlog::info("User {} Logout Status", user_id);

        tinyim::db::RedisClient redis;
        // 1. Set Status = 0 (Offline)
        redis.Set("user:status:" + std::to_string(user_id), "0");

        // 2. Get Friends (to notify them)
        auto friend_ids = auth_client_->GetFriendIds(user_id);

        // 3. Notify friends
        reply->set_success(true);
        for (int64_t fid : friend_ids) {
            auto status_opt = redis.Get("user:status:" + std::to_string(fid));
            if (status_opt && *status_opt == "1") {
                reply->add_online_friend_ids(fid); // Just to match proto, though logout res usually empty
                
                // Notify friend that I am offline
                spdlog::info("Notifying friend {} that user {} is offline", fid, user_id);
                NotifyUser(fid, user_id, 0);
            }
        }

        return Status::OK;
    }

private:
    void NotifyUser(int64_t target_user_id, int64_t status_user_id, int status) {
        tinyim::db::RedisClient redis;
        auto gateway_opt = redis.HGet("user_gateway", std::to_string(target_user_id));
        if (!gateway_opt) {
            spdlog::warn("User {} not found in user_gateway, cannot notify", target_user_id);
            return;
        }

        api::v1::GatewayMessage msg;
        msg.set_type(api::v1::MessageType::STATUS_UPDATE);
        auto* data = msg.mutable_status_data();
        data->set_user_id(status_user_id);
        data->set_status(status);

        std::string payload;
        msg.SerializeToString(&payload);
        std::string pub_msg = std::to_string(target_user_id) + "|" + payload;
        
        std::string channel = "gateway_" + *gateway_opt;
        spdlog::info("Publishing status update to channel {}: target={}, status_user={}, status={}", channel, target_user_id, status_user_id, status);
        tinyim::db::RedisPubSubClient::Instance().Publish(channel, pub_msg);
    }

    Status GetStatus(ServerContext* context, const GetStatusReq* request, GetStatusRes* reply) override {
        tinyim::db::RedisClient redis;
        auto* map = reply->mutable_status_map();
        
        for (int64_t uid : request->user_ids()) {
            auto status_opt = redis.Get("user:status:" + std::to_string(uid));
            int status = (status_opt && *status_opt == "1") ? 1 : 0;
            (*map)[uid] = status;
        }
        
        return Status::OK;
    }
};

void RunServer() {
    auto& config = tinyim::Config::Instance();
    // Default to 50053 if not set (we will add it to config later)
    int port = config.Server().status_port;
    if (port == 0) port = 50053; 

    std::string server_address("0.0.0.0:" + std::to_string(port));
    
    std::string auth_address = config.Services().auth_address;
    auto auth_client = std::make_shared<StatusAuthClient>(grpc::CreateChannel(auth_address, grpc::InsecureChannelCredentials()));

    StatusServiceImpl service(auth_client);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    spdlog::info("Status Server listening on {}", server_address);
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

    // Init Redis Pool
    tinyim::db::RedisPool::Instance().Init(tinyim::Config::Instance().Redis(), tinyim::Config::Instance().RedisSentinel());

    RunServer();
    return 0;
}
