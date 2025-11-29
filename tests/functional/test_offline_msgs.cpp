#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <grpcpp/grpcpp.h>
#include "auth_client.hpp"
#include "chat_client.hpp"
#include "config/config.hpp"
#include "log/logger.hpp"
#include "api/v1/gateway.pb.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// Simple blocking WebSocket Client
class WSClient {
    net::io_context ioc_;
    tcp::resolver resolver_;
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;

public:
    WSClient() : resolver_(ioc_), ws_(ioc_) {}

    void connect(const std::string& host, const std::string& port, const std::string& token) {
        auto const results = resolver_.resolve(host, port);
        net::connect(ws_.next_layer(), results.begin(), results.end());
        ws_.handshake(host, "/ws?token=" + token);
    }

    void close() {
        ws_.close(websocket::close_code::normal);
    }

    api::v1::GatewayMessage read() {
        ws_.read(buffer_);
        api::v1::GatewayMessage msg;
        msg.ParseFromArray(buffer_.data().data(), buffer_.data().size());
        buffer_.consume(buffer_.size());
        return msg;
    }
};

#define ASSERT_TRUE(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "[FAIL] " << message << std::endl; \
            std::exit(1); \
        } else { \
            std::cout << "[PASS] " << message << std::endl; \
        } \
    } while (0)

int main(int argc, char** argv) {
    tinyim::Logger::Init();
    std::string config_path = "configs/config.json";
    if (argc > 1) {
        config_path = argv[1];
    }
    if (!tinyim::Config::Instance().Load(config_path)) {
        std::cerr << "Failed to load config from " << config_path << std::endl;
        return 1;
    }

    std::string auth_address = tinyim::Config::Instance().Services().auth_address;
    std::string chat_address = tinyim::Config::Instance().Services().chat_address;
    int gateway_port = tinyim::Config::Instance().Server().gateway_port;
    
    AuthClient auth_client(grpc::CreateChannel(auth_address, grpc::InsecureChannelCredentials()));
    ChatClient chat_client(grpc::CreateChannel(chat_address, grpc::InsecureChannelCredentials()));

    // 1. Register User A and User B
    std::string suffix = std::to_string(std::time(nullptr));
    std::string userA = "userA_" + suffix;
    std::string userB = "userB_" + suffix;
    std::string password = "password";
    int64_t idA = 0, idB = 0;
    
    auth_client.Register(userA, password, idA);
    auth_client.Register(userB, password, idB);
    ASSERT_TRUE(idA > 0 && idB > 0, "Register Users");

    // 2. User A sends message to User B (who is offline)
    std::string content = "Offline Message " + suffix;
    int64_t msg_id = 0;
    // Note: We use ChatClient directly to simulate sending message. 
    // In reality, A would send via Gateway, but direct ChatClient call is fine to test storage.
    // Wait, if we use ChatClient directly, we bypass Gateway's "Online Check" if any?
    // ChatServer just saves it. Gateway usually handles routing.
    // If we use ChatClient::SaveMessage, it saves to DB.
    // Does it update session unread count? Yes, ChatServer logic does that.
    bool saved = chat_client.SaveMessage(idA, idB, content, std::time(nullptr) * 1000, msg_id);
    ASSERT_TRUE(saved, "User A sends offline message");

    // 3. User B logs in and connects
    std::string tokenB;
    int64_t uidB;
    auth_client.Login(userB, password, tokenB, uidB);
    ASSERT_TRUE(!tokenB.empty(), "User B Login");

    WSClient clientB;
    const char* gateway_host_env = std::getenv("GATEWAY_HOST");
    std::string gateway_host = gateway_host_env ? gateway_host_env : "localhost";
    clientB.connect(gateway_host, std::to_string(gateway_port), tokenB);
    std::cout << "User B connected to " << gateway_host << std::endl;

    // 4. Verify User B receives CHAT_PUSH
    bool received = false;
    for(int i=0; i<5; ++i) {
        auto msg = clientB.read();
        if(msg.type() == api::v1::MessageType::CHAT_PUSH) {
            if(msg.chat_data().content() == content) {
                received = true;
                break;
            }
        }
    }
    ASSERT_TRUE(received, "User B received offline message");

    std::cout << "Offline Message Test Passed!" << std::endl;
    return 0;
}
