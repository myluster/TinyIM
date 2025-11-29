#pragma once
#include <grpcpp/grpcpp.h>
#include "api/v1/chat.grpc.pb.h"
#include <memory>
#include <string>
#include <vector>

struct ChatMessage {
    int64_t msg_id;
    int64_t from_id;
    int64_t to_id;
    std::string content;
    int64_t timestamp;
};

class ChatClient {
public:
    ChatClient(std::shared_ptr<grpc::Channel> channel)
        : stub_(api::v1::ChatService::NewStub(channel)) {}

    bool SaveMessage(int64_t from_id, int64_t to_id, const std::string& content, int64_t timestamp, int64_t& msg_id) {
        api::v1::ChatPacket request;
        request.set_from_user_id(from_id);
        request.set_to_user_id(to_id);
        request.set_content(content);
        request.set_timestamp(timestamp);
        
        api::v1::SaveMessageRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->SaveMessage(&context, request, &reply);
        if (status.ok() && reply.success()) {
            msg_id = reply.msg_id();
            return true;
        }
        return false;
    }

    std::vector<ChatMessage> GetHistory(int64_t user_id, int64_t peer_id, int limit = 50) {
        api::v1::GetHistoryReq request;
        request.set_user_id(user_id);
        request.set_peer_id(peer_id);
        request.set_limit(limit);
        
        api::v1::GetHistoryRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->GetHistory(&context, request, &reply);
        
        std::vector<ChatMessage> history;
        if (status.ok()) {
            for (const auto& msg : reply.messages()) {
                history.push_back({msg.msg_id(), msg.from_user_id(), msg.to_user_id(), msg.content(), msg.timestamp()});
            }
        }
        return history;
    }

    struct Session {
        int64_t peer_id;
        std::string last_msg_content;
        int64_t last_msg_timestamp;
        int unread_count;
    };

    std::vector<Session> GetRecentSessions(int64_t user_id) {
        api::v1::GetRecentSessionsReq request;
        request.set_user_id(user_id);

        api::v1::GetRecentSessionsRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->GetRecentSessions(&context, request, &reply);

        std::vector<Session> sessions;
        if (status.ok()) {
            for (const auto& s : reply.sessions()) {
                sessions.push_back({s.peer_id(), s.last_msg_content(), s.last_msg_timestamp(), s.unread_count()});
            }
        }
        return sessions;
    }

    std::vector<ChatMessage> GetOfflineMessages(int64_t user_id) {
        api::v1::GetOfflineMessagesReq request;
        request.set_user_id(user_id);

        api::v1::GetOfflineMessagesRes reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->GetOfflineMessages(&context, request, &reply);

        std::vector<ChatMessage> messages;
        if (status.ok()) {
            for (const auto& msg : reply.messages()) {
                messages.push_back({msg.msg_id(), msg.from_user_id(), msg.to_user_id(), msg.content(), msg.timestamp()});
            }
        }
        return messages;
    }

private:
    std::unique_ptr<api::v1::ChatService::Stub> stub_;
};
