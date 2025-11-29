#include <boost/beast/core.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "auth_client.hpp"
#include "chat_client.hpp"
#include "status_client.hpp"
#include "log/logger.hpp"
#include "config/config.hpp"
#include "db/redis_client.hpp"
#include "server_context.hpp"
#include "session_manager.hpp"
#include "websocket_session.hpp"
#include "http_session.hpp"

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// Listener
class listener : public std::enable_shared_from_this<listener> {
    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::shared_ptr<ServerContext> context_;

public:
    listener(net::io_context& ioc, tcp::endpoint endpoint, std::shared_ptr<ServerContext> context)
        : ioc_(ioc), acceptor_(ioc), context_(context) {
        boost::beast::error_code ec;
        acceptor_.open(endpoint.protocol(), ec);
        if (ec) { spdlog::error("open: {}", ec.message()); return; }
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) { spdlog::error("set_option: {}", ec.message()); return; }
        acceptor_.bind(endpoint, ec);
        if (ec) { spdlog::error("bind: {}", ec.message()); return; }
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) { spdlog::error("listen: {}", ec.message()); return; }
    }

    void run() {
        do_accept();
    }

    void do_accept() {
        acceptor_.async_accept(net::make_strand(ioc_),
            boost::beast::bind_front_handler(&listener::on_accept, shared_from_this()));
    }

    void on_accept(boost::beast::error_code ec, tcp::socket socket) {
        if (ec) { spdlog::error("accept: {}", ec.message()); }
        else { 
            std::make_shared<http_session>(std::move(socket), context_)->run(); 
        }
        do_accept();
    }
};

// SessionManager Implementation
void SessionManager::join(int64_t user_id, websocket_session* session) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[user_id] = session;

    // Register to Redis
    tinyim::db::RedisClient redis;
    redis.HSet("user_gateway", std::to_string(user_id), gateway_id_);
    spdlog::info("User {} joined gateway {}", user_id, gateway_id_);
}

void SessionManager::leave(int64_t user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(user_id);

    // Remove from Redis
    tinyim::db::RedisClient redis;
    redis.HDel("user_gateway", std::to_string(user_id));
    spdlog::info("User {} left gateway {}", user_id, gateway_id_);
}

void SessionManager::send_to_user(int64_t user_id, const api::v1::GatewayMessage& message) {
    // 1. Check local session
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(user_id);
        if (it != sessions_.end()) {
            it->second->send_message(message);
            return;
        }
    }

    // 2. Check Redis for target gateway
    tinyim::db::RedisClient redis;
    auto target_gateway_opt = redis.HGet("user_gateway", std::to_string(user_id));
    if (!target_gateway_opt) {
        spdlog::warn("User {} not online", user_id);
        return;
    }

    std::string target_gateway = *target_gateway_opt;
    
    // 3. Publish to target gateway
    std::string payload;
    message.SerializeToString(&payload);
    std::string pub_msg = std::to_string(user_id) + "|" + payload;
    
    tinyim::db::RedisPubSubClient::Instance().Publish("gateway_" + target_gateway, pub_msg);
    spdlog::info("Forwarded message for user {} to gateway {}", user_id, target_gateway);
}

void SessionManager::send_to_local_user(int64_t user_id, const api::v1::GatewayMessage& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(user_id);
    if (it != sessions_.end()) {
        it->second->send_message(message);
    }
}

int main(int argc, char* argv[]) {
    tinyim::Logger::Init();
    
    std::string config_path = "configs/config.json";
    if (argc > 1) {
        config_path = argv[1];
    }

    if (!tinyim::Config::Instance().Load(config_path)) {
        spdlog::error("Failed to load config from {}", config_path);
        return 1;
    }
    
    // Get Gateway ID from env
    const char* gateway_id_env = std::getenv("GATEWAY_ID");
    std::string gateway_id = gateway_id_env ? gateway_id_env : "1";
    spdlog::info("Starting Gateway ID: {}", gateway_id);

    auto const address = net::ip::make_address("0.0.0.0");
    auto const port = static_cast<unsigned short>(tinyim::Config::Instance().Server().gateway_port);
    auto const threads = std::max<int>(1, std::thread::hardware_concurrency());

    auto context = std::make_shared<ServerContext>();
    
    std::string auth_address = tinyim::Config::Instance().Services().auth_address; 
    context->auth_client = std::make_shared<AuthClient>(grpc::CreateChannel(auth_address, grpc::InsecureChannelCredentials()));

    std::string chat_address = tinyim::Config::Instance().Services().chat_address;
    context->chat_client = std::make_shared<ChatClient>(grpc::CreateChannel(chat_address, grpc::InsecureChannelCredentials()));

    std::string status_address = tinyim::Config::Instance().Services().status_address;
    context->status_client = std::make_shared<StatusClient>(grpc::CreateChannel(status_address, grpc::InsecureChannelCredentials()));

    context->session_manager = std::make_shared<SessionManager>(gateway_id);
    context->thread_pool = std::make_shared<boost::asio::thread_pool>(4);

    // Init Redis Pools
    tinyim::db::RedisPool::Instance().Init(tinyim::Config::Instance().Redis(), tinyim::Config::Instance().RedisSentinel());

    // Init Redis Pub/Sub
    tinyim::db::RedisPubSubClient::Instance().Subscribe("gateway_" + gateway_id, [context](const std::string& channel, const std::string& msg) {
        spdlog::info("Received Pub/Sub message on channel: {}, length: {}", channel, msg.length());
        
        auto pos = msg.find('|');
        if (pos == std::string::npos) {
            spdlog::warn("Invalid message format (no delimiter): {}", msg);
            return;
        }
        
        try {
            int64_t user_id = std::stoll(msg.substr(0, pos));
            std::string payload = msg.substr(pos + 1);
            
            spdlog::info("Parsed message: user_id={}, payload_length={}", user_id, payload.length());
            
            api::v1::GatewayMessage gateway_msg;
            if (gateway_msg.ParseFromString(payload)) {
                spdlog::info("Successfully parsed GatewayMessage, type={}", gateway_msg.type());
                context->session_manager->send_to_local_user(user_id, gateway_msg);
            } else {
                spdlog::error("Failed to parse GatewayMessage from payload");
            }
        } catch (...) {
            spdlog::error("Failed to parse Redis Pub/Sub message");
        }
    });
    tinyim::db::RedisPubSubClient::Instance().Init(tinyim::Config::Instance().Redis());

    net::io_context ioc{threads};
    std::make_shared<listener>(ioc, tcp::endpoint{address, port}, context)->run();

    spdlog::info("Gateway listening on {}:{}", address.to_string(), port);

    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for(auto i = threads - 1; i > 0; --i)
        v.emplace_back([&ioc]{ ioc.run(); });
    ioc.run();
    
    context->thread_pool->join();
    tinyim::db::RedisPubSubClient::Instance().Stop();

    return 0;
}
