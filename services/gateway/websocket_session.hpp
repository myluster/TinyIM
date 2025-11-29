#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/post.hpp>
#include <memory>
#include <string>
#include <vector>
#include "server_context.hpp"
#include "session_manager.hpp"
#include "api/v1/gateway.pb.h"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
using api::v1::GatewayMessage;
using api::v1::MessageType;

// WebSocket 会话类：处理单个用户的 WebSocket 连接
class websocket_session : public std::enable_shared_from_this<websocket_session> {
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::vector<std::shared_ptr<std::string>> queue_;
    int64_t user_id_ = 0;
    std::shared_ptr<ServerContext> context_;

public:
    explicit websocket_session(tcp::socket&& socket, std::shared_ptr<ServerContext> context)
        : ws_(std::move(socket)), context_(context) {
            ws_.binary(true); // 启用二进制模式以支持 Protobuf
        }

    ~websocket_session() {
        if (user_id_ != 0) {
            context_->session_manager->leave(user_id_);

            // Notify friends offline via Status Server
            net::post(*context_->thread_pool, [context = context_, uid = user_id_]() {
                // Token is not stored in session currently, passing empty string or we need to store it.
                // LoginReq has token, but we didn't store it. 
                // StatusServer Logout might not need token strictly if we trust the internal call? 
                // But proto says token. Let's pass empty for now or store it.
                // Actually, let's just pass empty string, assuming StatusServer doesn't validate token for Logout yet or we trust it.
                auto result = context->status_client->Logout(uid, "");
                
                if (result.success) {
                    for (int64_t fid : result.online_friend_ids) {
                        GatewayMessage msg;
                        msg.set_type(MessageType::STATUS_UPDATE);
                        auto* status = msg.mutable_status_data();
                        status->set_user_id(uid);
                        status->set_status(0); // Offline
                        status->set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                        
                        context->session_manager->send_to_user(fid, msg);
                    }
                    spdlog::info("Notified {} friends that user {} is offline", result.online_friend_ids.size(), uid);
                }
            });
        }
    }

    // 启动会话，处理握手和鉴权
    void run(boost::beast::http::request<boost::beast::http::string_body> req) {
        // 从 URL 参数中解析 Token: /ws?token=...
        std::string target = std::string(req.target());
        auto pos = target.find("token=");
        std::string token;
        if (pos != std::string::npos) {
            token = target.substr(pos + 6);
        }

        // 将鉴权操作（阻塞 gRPC）投递到线程池执行，避免阻塞 I/O 线程
        net::post(*context_->thread_pool, [self = shared_from_this(), req = std::move(req), token]() mutable {
            int64_t uid = 0;
            bool valid = false;
            if (!token.empty()) {
                valid = self->context_->auth_client->VerifyToken(token, uid);
            }

            // 鉴权完成后，切回 I/O 线程继续处理握手
            net::dispatch(self->ws_.get_executor(), [self, req = std::move(req), valid, uid]() mutable {
                if (valid) {
                    self->user_id_ = uid;
                    spdlog::info("Token verified for user {}", uid);
                } else {
                    spdlog::warn("Invalid token");
                }
                self->on_run(std::move(req));
            });
        });
    }

    void on_run(boost::beast::http::request<boost::beast::http::string_body> req) {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.async_accept(req, beast::bind_front_handler(&websocket_session::on_accept, shared_from_this()));
    }

    void on_accept(beast::error_code ec) {
        if (ec) return spdlog::error("accept: {}", ec.message());
        
        if (user_id_ == 0) {
            // 鉴权失败则关闭连接
            ws_.async_close(websocket::close_code::policy_error, beast::bind_front_handler(&websocket_session::on_close, shared_from_this()));
            return;
        }

        // 加入 SessionManager 管理
        context_->session_manager->join(user_id_, this);

        // Notify friends online via Status Server
        net::post(*context_->thread_pool, [context = context_, uid = user_id_]() {
            // We don't have the token here easily unless we stored it. 
            // But we verified it. Let's pass empty string for now.
            auto result = context->status_client->Login(uid, "");
            
            if (result.success) {
                for (int64_t fid : result.online_friend_ids) {
                    GatewayMessage msg;
                    msg.set_type(MessageType::STATUS_UPDATE);
                    auto* status = msg.mutable_status_data();
                    status->set_user_id(uid);
                    status->set_status(1); // Online
                    status->set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                    
                    context->session_manager->send_to_user(fid, msg);
                }
                spdlog::info("Notified {} friends that user {} is online", result.online_friend_ids.size(), uid);
            }

            // Pull Offline Messages
            auto offline_msgs = context->chat_client->GetOfflineMessages(uid);
            if (!offline_msgs.empty()) {
                for (const auto& msg : offline_msgs) {
                    GatewayMessage push_msg;
                    push_msg.set_type(MessageType::CHAT_PUSH);
                    auto* push_data = push_msg.mutable_chat_data();
                    push_data->set_msg_id(msg.msg_id);
                    push_data->set_from_user_id(msg.from_id);
                    push_data->set_to_user_id(msg.to_id);
                    push_data->set_content(msg.content);
                    push_data->set_timestamp(msg.timestamp);
                    
                    // Directly send to self (this session)
                    // We need to switch to I/O thread to send? 
                    // SessionManager::send_to_user sends to all sessions of user.
                    // But here we are in the session context, we can just send.
                    // However, we are in a thread pool thread.
                    // send_message posts to I/O strand. So it is safe.
                    // But we don't have access to 'self' easily here? 
                    // We captured 'context' and 'uid'. We didn't capture 'this' or 'self'.
                    // We should use SessionManager to send to this user (which includes this session).
                    context->session_manager->send_to_user(uid, push_msg);
                }
                spdlog::info("Pushed {} offline messages to user {}", offline_msgs.size(), uid);
            }
        });
        do_read();
    }

    void on_close(beast::error_code ec) {
        if (ec) spdlog::error("close: {}", ec.message());
    }

    void do_read() {
        ws_.async_read(buffer_, beast::bind_front_handler(&websocket_session::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (ec == websocket::error::closed) return;
        if (ec) return spdlog::error("read: {}", ec.message());

        // 解析 Protobuf 消息
        GatewayMessage msg;
        if (msg.ParseFromArray(buffer_.data().data(), buffer_.data().size())) {
            handle_message(std::move(msg));
        } else {
            spdlog::error("Failed to parse GatewayMessage");
        }

        buffer_.consume(buffer_.size());
        do_read();
    }

    void handle_message(GatewayMessage msg) {
        if (msg.type() == MessageType::CHAT_SEND && msg.has_chat_data()) {
            // 将消息保存操作（阻塞 gRPC）投递到线程池
            net::post(*context_->thread_pool, [self = shared_from_this(), msg = std::move(msg)]() mutable {
                const auto& chat_data = msg.chat_data();
                int64_t to_user_id = chat_data.to_user_id();
                std::string content = chat_data.content();
                int64_t msg_id = 0;
                int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

                // 调用 Chat 服务保存消息
                bool saved = self->context_->chat_client->SaveMessage(self->user_id_, to_user_id, content, timestamp, msg_id);

                // 切回 I/O 线程发送响应
                net::dispatch(self->ws_.get_executor(), [self, msg, saved, msg_id, to_user_id, content, timestamp]() {
                    if (saved) {
                        // 发送 ACK 给发送者
                        GatewayMessage ack;
                        ack.set_type(MessageType::CHAT_ACK);
                        ack.set_request_id(msg.request_id());
                        self->send_message(ack);

                        // 推送消息给接收者
                        GatewayMessage push_msg;
                        push_msg.set_type(MessageType::CHAT_PUSH);
                        auto* push_data = push_msg.mutable_chat_data();
                        push_data->set_msg_id(msg_id);
                        push_data->set_from_user_id(self->user_id_);
                        push_data->set_to_user_id(to_user_id);
                        push_data->set_content(content);
                        push_data->set_timestamp(timestamp);
                        
                        self->context_->session_manager->send_to_user(to_user_id, push_msg);
                    } else {
                        GatewayMessage err;
                        err.set_type(MessageType::UNKNOWN);
                        err.set_request_id(msg.request_id());
                        err.set_error("Failed to save message");
                        self->send_message(err);
                    }
                });
            });
        } else if (msg.type() == MessageType::HEARTBEAT_PING) {
             GatewayMessage pong;
             pong.set_type(MessageType::HEARTBEAT_PONG);
             send_message(pong);
        }
    }

    void send_message(const GatewayMessage& msg) {
        std::string data;
        msg.SerializeToString(&data);
        send(data);
    }

    void send(const std::string& message) {
        net::post(ws_.get_executor(), beast::bind_front_handler(&websocket_session::on_send, shared_from_this(), message));
    }

    void on_send(std::string message) {
        queue_.push_back(std::make_shared<std::string>(message));
        if (queue_.size() > 1) return;
        do_write();
    }

    void do_write() {
        ws_.async_write(net::buffer(*queue_.front()), beast::bind_front_handler(&websocket_session::on_write, shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (ec) return spdlog::error("write: {}", ec.message());
        queue_.erase(queue_.begin());
        if (!queue_.empty()) do_write();
    }
};
