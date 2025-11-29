#pragma once
#include <grpcpp/grpcpp.h>
#include "api/v1/status.grpc.pb.h"
#include <memory>
#include <vector>
#include <map>

class StatusClient {
public:
    StatusClient(std::shared_ptr<grpc::Channel> channel)
        : stub_(api::v1::StatusService::NewStub(channel)) {}

    struct LoginResult {
        bool success;
        std::vector<int64_t> online_friend_ids;
    };

    LoginResult Login(int64_t user_id, const std::string& token) {
        api::v1::LoginStatusReq request;
        request.set_user_id(user_id);
        request.set_token(token);
        api::v1::LoginStatusRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->Login(&context, request, &reply);
        
        LoginResult result;
        result.success = false;
        if (status.ok() && reply.success()) {
            result.success = true;
            for (auto id : reply.online_friend_ids()) {
                result.online_friend_ids.push_back(id);
            }
        }
        return result;
    }

    struct LogoutResult {
        bool success;
        std::vector<int64_t> online_friend_ids;
    };

    LogoutResult Logout(int64_t user_id, const std::string& token) {
        api::v1::LogoutStatusReq request;
        request.set_user_id(user_id);
        request.set_token(token);
        api::v1::LogoutStatusRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->Logout(&context, request, &reply);
        
        LogoutResult result;
        result.success = false;
        if (status.ok() && reply.success()) {
            result.success = true;
            for (auto id : reply.online_friend_ids()) {
                result.online_friend_ids.push_back(id);
            }
        }
        return result;
    }

    std::map<int64_t, int> GetStatus(const std::vector<int64_t>& user_ids) {
        api::v1::GetStatusReq request;
        for (auto id : user_ids) {
            request.add_user_ids(id);
        }
        api::v1::GetStatusRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->GetStatus(&context, request, &reply);
        
        std::map<int64_t, int> status_map;
        if (status.ok()) {
            for (auto const& [uid, stat] : reply.status_map()) {
                status_map[uid] = stat;
            }
        }
        return status_map;
    }

private:
    std::unique_ptr<api::v1::StatusService::Stub> stub_;
};
