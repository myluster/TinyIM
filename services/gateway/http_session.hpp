#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/post.hpp>
#include <memory>
#include <string>
#include <vector>
#include "server_context.hpp"
#include "websocket_session.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// 辅助函数：创建 JSON 响应
inline std::string create_json_response(bool success, const std::string& message, const std::string& token = "", int64_t user_id = 0) {
    std::string json = "{";
    json += "\"success\": " + std::string(success ? "true" : "false") + ",";
    json += "\"message\": \"" + message + "\"";
    if (!token.empty()) {
        json += ", \"token\": \"" + token + "\"";
    }
    if (user_id != 0) {
        json += ", \"user_id\": " + std::to_string(user_id);
    }
    json += "}";
    return json;
}

// HTTP 会话类：处理 HTTP API 请求
class http_session : public std::enable_shared_from_this<http_session> {
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    std::shared_ptr<ServerContext> context_;

public:
    explicit http_session(tcp::socket&& socket, std::shared_ptr<ServerContext> context)
        : stream_(std::move(socket)), context_(context) {}

    void run() {
        net::dispatch(stream_.get_executor(), beast::bind_front_handler(&http_session::do_read, shared_from_this()));
    }

    void do_read() {
        req_ = {};
        stream_.expires_after(std::chrono::seconds(30));
        http::async_read(stream_, buffer_, req_, beast::bind_front_handler(&http_session::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (ec == http::error::end_of_stream) {
            stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
            return;
        }
        if (ec) return spdlog::error("read: {}", ec.message());

        // 如果是 WebSocket 升级请求，转交给 websocket_session 处理
        if (websocket::is_upgrade(req_)) {
            std::make_shared<websocket_session>(stream_.release_socket(), context_)->run(std::move(req_));
            return;
        }

        handle_request();
    }

    void handle_request() {
        spdlog::info("HTTP Request: {} {}", std::string(req_.method_string()), std::string(req_.target()));

        // 将请求处理（可能包含阻塞 gRPC）投递到线程池
        net::post(*context_->thread_pool, [self = shared_from_this(), req = req_]() mutable {
            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::server, "TinyIM Gateway");
            res.set(http::field::content_type, "application/json");
            res.set(http::field::access_control_allow_origin, "*");
            res.keep_alive(req.keep_alive());

            if (req.method() == http::verb::post && req.target() == "/api/login") {
                // ... (解析参数)
                std::string body = req.body();
                std::string username, password;
                auto parse_kv = [&](const std::string& s) {
                    auto pos = s.find('=');
                    if (pos != std::string::npos) return std::make_pair(s.substr(0, pos), s.substr(pos + 1));
                    return std::make_pair(std::string(), std::string());
                };
                
                size_t start = 0, end = 0;
                while ((end = body.find('&', start)) != std::string::npos) {
                    auto kv = parse_kv(body.substr(start, end - start));
                    if (kv.first == "username") username = kv.second;
                    else if (kv.first == "password") password = kv.second;
                    start = end + 1;
                }
                auto kv = parse_kv(body.substr(start));
                if (kv.first == "username") username = kv.second;
                else if (kv.first == "password") password = kv.second;

                std::string token;
                int64_t user_id;
                // 调用 Auth 服务进行登录（阻塞操作，但在线程池中执行）
                if (self->context_->auth_client->Login(username, password, token, user_id)) {
                    res.body() = create_json_response(true, "Login successful", token, user_id);
                } else {
                    res.result(http::status::unauthorized);
                    res.body() = create_json_response(false, "Login failed");
                }
            } else if (req.method() == http::verb::post && req.target() == "/api/register") {
                // ... (注册逻辑)
                std::string body = req.body();
                std::string username, password;
                auto parse_kv = [&](const std::string& s) {
                    auto pos = s.find('=');
                    if (pos != std::string::npos) return std::make_pair(s.substr(0, pos), s.substr(pos + 1));
                    return std::make_pair(std::string(), std::string());
                };
                
                size_t start = 0, end = 0;
                while ((end = body.find('&', start)) != std::string::npos) {
                    auto kv = parse_kv(body.substr(start, end - start));
                    if (kv.first == "username") username = kv.second;
                    else if (kv.first == "password") password = kv.second;
                    start = end + 1;
                }
                auto kv = parse_kv(body.substr(start));
                if (kv.first == "username") username = kv.second;
                else if (kv.first == "password") password = kv.second;

                int64_t user_id;
                if (self->context_->auth_client->Register(username, password, user_id)) {
                     res.body() = create_json_response(true, "Register successful", "", user_id);
                } else {
                     res.body() = create_json_response(false, "Register failed");
                }
            } else if (req.method() == http::verb::post && req.target() == "/api/friend/add") {
                std::string body = req.body();
                std::string token, friend_id_str;
                auto parse_kv = [&](const std::string& s) {
                    auto pos = s.find('=');
                    if (pos != std::string::npos) return std::make_pair(s.substr(0, pos), s.substr(pos + 1));
                    return std::make_pair(std::string(), std::string());
                };
                
                size_t start = 0, end = 0;
                while ((end = body.find('&', start)) != std::string::npos) {
                    auto kv = parse_kv(body.substr(start, end - start));
                    if (kv.first == "token") token = kv.second;
                    else if (kv.first == "friend_id") friend_id_str = kv.second;
                    start = end + 1;
                }
                auto kv = parse_kv(body.substr(start));
                if (kv.first == "token") token = kv.second;
                else if (kv.first == "friend_id") friend_id_str = kv.second;

                int64_t user_id = 0;
                if (!self->context_->auth_client->VerifyToken(token, user_id)) {
                    res.result(http::status::unauthorized);
                    res.body() = create_json_response(false, "Invalid token");
                } else {
                    std::string error_msg;
                    int64_t friend_id = friend_id_str.empty() ? 0 : std::stoll(friend_id_str);
                    if (self->context_->auth_client->AddFriend(user_id, friend_id, error_msg)) {
                        res.body() = create_json_response(true, "Friend request sent");
                    } else {
                        res.body() = create_json_response(false, error_msg);
                    }
                }
            } else if (req.method() == http::verb::post && req.target() == "/api/friend/request/handle") {
                std::string body = req.body();
                std::string token, request_id_str, accept_str;
                auto parse_kv = [&](const std::string& s) {
                    auto pos = s.find('=');
                    if (pos != std::string::npos) return std::make_pair(s.substr(0, pos), s.substr(pos + 1));
                    return std::make_pair(std::string(), std::string());
                };
                
                size_t start = 0, end = 0;
                while ((end = body.find('&', start)) != std::string::npos) {
                    auto kv = parse_kv(body.substr(start, end - start));
                    if (kv.first == "token") token = kv.second;
                    else if (kv.first == "request_id") request_id_str = kv.second;
                    else if (kv.first == "accept") accept_str = kv.second;
                    start = end + 1;
                }
                auto kv = parse_kv(body.substr(start));
                if (kv.first == "token") token = kv.second;
                else if (kv.first == "request_id") request_id_str = kv.second;
                else if (kv.first == "accept") accept_str = kv.second;

                int64_t user_id = 0;
                if (!self->context_->auth_client->VerifyToken(token, user_id)) {
                    res.result(http::status::unauthorized);
                    res.body() = create_json_response(false, "Invalid token");
                } else {
                    std::string error_msg;
                    int64_t request_id = request_id_str.empty() ? 0 : std::stoll(request_id_str);
                    bool accept = (accept_str == "true");
                    if (self->context_->auth_client->HandleFriendRequest(user_id, request_id, accept, error_msg)) {
                        res.body() = create_json_response(true, "Request handled");
                    } else {
                        res.body() = create_json_response(false, error_msg);
                    }
                }
            } else {
                // GET requests
                auto parse_query = [&](const std::string& target, const std::string& key) {
                    auto pos = target.find(key + "=");
                    if (pos == std::string::npos) return std::string();
                    auto end = target.find('&', pos);
                    if (end == std::string::npos) return target.substr(pos + key.length() + 1);
                    return target.substr(pos + key.length() + 1, end - pos - key.length() - 1);
                };

                if (req.method() == http::verb::get && req.target().starts_with("/api/history")) {
                    std::string target = std::string(req.target());
                    std::string token = parse_query(target, "token");
                    std::string peer_id_str = parse_query(target, "peer_id");
                    int64_t user_id = 0;
                    
                    if (!self->context_->auth_client->VerifyToken(token, user_id)) {
                        res.result(http::status::unauthorized);
                        res.body() = create_json_response(false, "Invalid token");
                    } else {
                        int64_t peer_id = std::stoll(peer_id_str.empty() ? "0" : peer_id_str);
                        auto history = self->context_->chat_client->GetHistory(user_id, peer_id);
                        
                        std::string json = "{\"success\": true, \"messages\": [";
                        for (size_t i = 0; i < history.size(); ++i) {
                            const auto& msg = history[i];
                            json += "{\"msg_id\": " + std::to_string(msg.msg_id) + 
                                    ", \"from\": " + std::to_string(msg.from_id) + 
                                    ", \"to\": " + std::to_string(msg.to_id) + 
                                    ", \"content\": \"" + msg.content + "\"" + 
                                    ", \"timestamp\": " + std::to_string(msg.timestamp) + "}";
                            if (i < history.size() - 1) json += ",";
                        }
                        json += "]}";
                        res.body() = json;
                    }
                } else if (req.method() == http::verb::get && req.target().starts_with("/api/friend/list")) {
                    std::string target = std::string(req.target());
                    std::string token = parse_query(target, "token");
                    int64_t user_id = 0;
                    
                    if (!self->context_->auth_client->VerifyToken(token, user_id)) {
                        res.result(http::status::unauthorized);
                        res.body() = create_json_response(false, "Invalid token");
                    } else {
                        auto friends = self->context_->auth_client->GetFriendList(user_id);
                        std::string json = "{\"success\": true, \"friends\": [";
                        for (size_t i = 0; i < friends.size(); ++i) {
                            const auto& f = friends[i];
                            json += "{\"user_id\": " + std::to_string(f.user_id) + 
                                    ", \"username\": \"" + f.username + "\"" + 
                                    ", \"status\": " + std::to_string(f.status) + "}";
                            if (i < friends.size() - 1) json += ",";
                        }
                        json += "]}";
                        res.body() = json;
                    }
                } else if (req.method() == http::verb::get && req.target().starts_with("/api/friend/requests")) {
                    std::string target = std::string(req.target());
                    std::string token = parse_query(target, "token");
                    int64_t user_id = 0;
                    
                    if (!self->context_->auth_client->VerifyToken(token, user_id)) {
                        res.result(http::status::unauthorized);
                        res.body() = create_json_response(false, "Invalid token");
                    } else {
                        auto requests = self->context_->auth_client->GetPendingFriendRequests(user_id);
                        std::string json = "{\"success\": true, \"requests\": [";
                        for (size_t i = 0; i < requests.size(); ++i) {
                            const auto& r = requests[i];
                            json += "{\"request_id\": " + std::to_string(r.request_id) + 
                                    ", \"sender_id\": " + std::to_string(r.sender_id) + 
                                    ", \"sender_username\": \"" + r.sender_username + "\"" + 
                                    ", \"created_at\": " + std::to_string(r.created_at) + "}";
                            if (i < requests.size() - 1) json += ",";
                        }
                        json += "]}";
                        res.body() = json;
                    }
                } else if (req.method() == http::verb::get && req.target().starts_with("/api/sessions")) {
                    std::string target = std::string(req.target());
                    std::string token = parse_query(target, "token");
                    int64_t user_id = 0;
                    
                    if (!self->context_->auth_client->VerifyToken(token, user_id)) {
                        res.result(http::status::unauthorized);
                        res.body() = create_json_response(false, "Invalid token");
                    } else {
                        auto sessions = self->context_->chat_client->GetRecentSessions(user_id);
                        std::string json = "{\"success\": true, \"sessions\": [";
                        for (size_t i = 0; i < sessions.size(); ++i) {
                            const auto& s = sessions[i];
                            json += "{\"peer_id\": " + std::to_string(s.peer_id) + 
                                    ", \"last_msg\": \"" + s.last_msg_content + "\"" + 
                                    ", \"timestamp\": " + std::to_string(s.last_msg_timestamp) + 
                                    ", \"unread\": " + std::to_string(s.unread_count) + "}";
                            if (i < sessions.size() - 1) json += ",";
                        }
                        json += "]}";
                        res.body() = json;
                    }
                } else {
                    res.result(http::status::not_found);
                    res.body() = create_json_response(false, "Not found (or not ported yet)");
                }
            }
            
            res.prepare_payload();
            auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));
            
            // 切回 I/O 线程发送响应
            net::dispatch(self->stream_.get_executor(), [self, sp]() {
                http::async_write(self->stream_, *sp, beast::bind_front_handler(&http_session::on_write, self, sp));
            });
        });
    }

    void on_write(std::shared_ptr<http::response<http::string_body>> sp, beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (ec) return spdlog::error("write: {}", ec.message());
        do_read();
    }
};
