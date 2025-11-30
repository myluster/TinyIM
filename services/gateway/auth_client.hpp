#pragma once
#include <grpcpp/grpcpp.h>
#include "api/v1/auth.grpc.pb.h"
#include <memory>
#include <string>

class AuthClient {
public:
    AuthClient(std::shared_ptr<grpc::Channel> channel)
        : stub_(api::v1::AuthService::NewStub(channel)) {}

    bool Login(const std::string& username, const std::string& password, std::string& token, int64_t& user_id) {
        api::v1::LoginReq request;
        request.set_username(username);
        request.set_password(password);
        api::v1::LoginRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->Login(&context, request, &reply);
        if (status.ok() && reply.success()) {
            token = reply.token();
            user_id = reply.user_id();
            return true;
        }
        return false;
    }

    bool Register(const std::string& username, const std::string& password, int64_t& user_id) {
        api::v1::RegisterReq request;
        request.set_username(username);
        request.set_password(password);
        api::v1::RegisterRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->Register(&context, request, &reply);
        if (status.ok() && reply.success()) {
            user_id = reply.user_id();
            return true;
        }
        return false;
    }

    bool VerifyToken(const std::string& token, int64_t& user_id) {
        api::v1::VerifyTokenReq request;
        request.set_token(token);
        api::v1::VerifyTokenRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->VerifyToken(&context, request, &reply);
        if (status.ok() && reply.valid()) {
            user_id = reply.user_id();
            return true;
        }
        return false;
    }

    bool AddFriend(int64_t user_id, int64_t friend_id, std::string& error_msg) {
        api::v1::AddFriendReq request;
        request.set_user_id(user_id);
        request.set_friend_id(friend_id);
        api::v1::AddFriendRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->AddFriend(&context, request, &reply);
        if (status.ok() && reply.success()) {
            return true;
        }
        error_msg = reply.error_msg();
        return false;
    }

    struct Friend {
        int64_t user_id;
        std::string username;
        int status;
    };

    std::vector<Friend> GetFriendList(int64_t user_id) {
        api::v1::GetFriendListReq request;
        request.set_user_id(user_id);
        api::v1::GetFriendListRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->GetFriendList(&context, request, &reply);
        
        std::vector<Friend> friends;
        if (status.ok() && reply.success()) {
            for (const auto& f : reply.friends()) {
                friends.push_back({f.user_id(), f.username(), f.status()});
            }
        }
        return friends;
    }

    bool HandleFriendRequest(int64_t user_id, int64_t sender_id, bool accept, std::string& error_msg) {
        api::v1::HandleFriendRequestReq request;
        request.set_user_id(user_id);
        request.set_request_id(sender_id);
        request.set_accept(accept);
        api::v1::HandleFriendRequestRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->HandleFriendRequest(&context, request, &reply);
        if (status.ok() && reply.success()) {
            return true;
        }
        error_msg = reply.error_msg();
        return false;
    }

    struct FriendRequest {
        int64_t request_id;
        int64_t sender_id;
        std::string sender_username;
        int64_t created_at;
    };

    std::vector<FriendRequest> GetPendingFriendRequests(int64_t user_id) {
        api::v1::GetPendingFriendRequestsReq request;
        request.set_user_id(user_id);
        api::v1::GetPendingFriendRequestsRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->GetPendingFriendRequests(&context, request, &reply);

        std::vector<FriendRequest> requests;
        if (status.ok() && reply.success()) {
            for (const auto& r : reply.requests()) {
                requests.push_back({r.request_id(), r.sender_id(), r.sender_username(), r.created_at()});
            }
        }
        return requests;
    }

    bool DeleteFriend(int64_t user_id, int64_t friend_id, std::string& error_msg) {
        api::v1::DeleteFriendReq request;
        request.set_user_id(user_id);
        request.set_friend_id(friend_id);
        api::v1::DeleteFriendRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->DeleteFriend(&context, request, &reply);
        if (status.ok() && reply.success()) {
            return true;
        }
        error_msg = reply.error_msg();
        return false;
    }

private:
    std::unique_ptr<api::v1::AuthService::Stub> stub_;
};
