#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <ctime>
#include <grpcpp/grpcpp.h>
#include "auth_client.hpp"
#include "chat_client.hpp"
#include "config/config.hpp"
#include "log/logger.hpp"

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

    std::cout << "Connecting to Auth Service at " << auth_address << std::endl;
    AuthClient auth_client(grpc::CreateChannel(auth_address, grpc::InsecureChannelCredentials()));

    std::cout << "Connecting to Chat Service at " << chat_address << std::endl;
    ChatClient chat_client(grpc::CreateChannel(chat_address, grpc::InsecureChannelCredentials()));

    // Test 1: Register
    std::string username = "testuser_" + std::to_string(std::time(nullptr));
    std::string password = "password123";
    int64_t user_id = 0;
    bool registered = auth_client.Register(username, password, user_id);
    ASSERT_TRUE(registered && user_id > 0, "User Registration");

    // Test 2: Login
    std::string token;
    int64_t login_uid = 0;
    bool logged_in = auth_client.Login(username, password, token, login_uid);
    std::cerr << "Login result: success=" << logged_in << ", user_id=" << login_uid << std::endl;
    ASSERT_TRUE(logged_in && !token.empty() && login_uid == user_id, "User Login");

    // Test 3: Verify Token
    int64_t verified_uid = 0;
    bool verified = auth_client.VerifyToken(token, verified_uid);
    ASSERT_TRUE(verified && verified_uid == user_id, "Token Verification");

    // Test 4: Send Message (SaveMessage)
    // We need another user to send to.
    std::string user2 = "testuser2_" + std::to_string(std::time(nullptr));
    int64_t user2_id = 0;
    auth_client.Register(user2, password, user2_id);
    ASSERT_TRUE(user2_id > 0, "User 2 Registration");

    int64_t msg_id = 0;
    std::string content = "Hello from functional test";
    bool saved = chat_client.SaveMessage(user_id, user2_id, content, std::time(nullptr), msg_id);
    ASSERT_TRUE(saved && msg_id > 0, "Save Message");

    // Test 5: Get Recent Sessions (for User 2)
    auto sessions = chat_client.GetRecentSessions(user2_id);
    bool session_found = false;
    for (const auto& s : sessions) {
        if (s.peer_id == user_id) {
            session_found = true;
            ASSERT_TRUE(s.last_msg_content == content, "Session Content Match");
            break;
        }
    }
    ASSERT_TRUE(session_found, "Get Recent Sessions");
    
    std::cout << "All Functional Tests Passed!" << std::endl;
    return 0;
}
