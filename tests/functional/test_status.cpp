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
    int gateway_port = tinyim::Config::Instance().Server().gateway_port;
    
    std::cout << "Connecting to Auth Service at " << auth_address << std::endl;
    AuthClient auth_client(grpc::CreateChannel(auth_address, grpc::InsecureChannelCredentials()));

    // 1. Register User A and User B
    std::string suffix = std::to_string(std::time(nullptr));
    std::string userA = "userA_" + suffix;
    std::string userB = "userB_" + suffix;
    std::string password = "password";
    int64_t idA = 0, idB = 0;
    
    auth_client.Register(userA, password, idA);
    auth_client.Register(userB, password, idB);
    ASSERT_TRUE(idA > 0 && idB > 0, "Register Users");

    // Wait for potential replication/consistency (small delay)
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 2. Make them friends
    std::string tokenA, tokenB;
    int64_t uid;
    auth_client.Login(userA, password, tokenA, uid);
    auth_client.Login(userB, password, tokenB, uid);

    std::string err;
    bool added = auth_client.AddFriend(idA, idB, err); // A adds B
    ASSERT_TRUE(added, "Add Friend Request");

    // B accepts A
    auto requests = auth_client.GetPendingFriendRequests(idB);
    int64_t req_id = 0;
    for(auto& r : requests) {
        if(r.sender_id == idA) {
            req_id = r.request_id;
            break;
        }
    }
    ASSERT_TRUE(req_id > 0, "Find Friend Request");
    
    bool handled = auth_client.HandleFriendRequest(idB, idA, true, err);
    ASSERT_TRUE(handled, "Accept Friend Request");

    // 3. User A connects
    const char* gateway_host_env = std::getenv("GATEWAY_HOST");
    std::string gateway_host = gateway_host_env ? gateway_host_env : "localhost";
    
    WSClient clientA;
    clientA.connect(gateway_host, std::to_string(gateway_port), tokenA);
    std::cout << "User A connected to " << gateway_host << std::endl;

    // 4. User B connects
    WSClient clientB;
    clientB.connect(gateway_host, std::to_string(gateway_port), tokenB);
    std::cout << "User B connected to " << gateway_host << std::endl;

    // 5. Verify User A receives STATUS_UPDATE (Online)
    bool received_online = false;
    for(int i=0; i<5; ++i) {
        auto msg = clientA.read();
        if(msg.type() == api::v1::MessageType::STATUS_UPDATE) {
            if(msg.status_data().user_id() == idB && msg.status_data().status() == 1) {
                received_online = true;
                break;
            }
        }
    }
    ASSERT_TRUE(received_online, "User A received User B Online");

    // 6. User B disconnects
    clientB.close();
    std::cout << "User B disconnected" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 7. Verify User A receives STATUS_UPDATE (Offline)
    bool received_offline = false;
    for(int i=0; i<5; ++i) {
        auto msg = clientA.read();
        if(msg.type() == api::v1::MessageType::STATUS_UPDATE) {
            if(msg.status_data().user_id() == idB && msg.status_data().status() == 0) {
                received_offline = true;
                break;
            }
        }
    }
    ASSERT_TRUE(received_offline, "User A received User B Offline");

    std::cout << "Status Broadcasting Test Passed!" << std::endl;
    return 0;
}
