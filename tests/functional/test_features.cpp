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

    // --- Setup Users ---
    std::string suffix = std::to_string(std::time(nullptr));
    std::string userA = "userA_" + suffix;
    std::string userB = "userB_" + suffix;
    std::string password = "password";
    int64_t idA = 0, idB = 0;
    
    ASSERT_TRUE(auth_client.Register(userA, password, idA), "Register User A");
    ASSERT_TRUE(auth_client.Register(userB, password, idB), "Register User B");
    ASSERT_TRUE(idA > 0 && idB > 0, "Users Registered");

    // --- Test 1: AckMessages (Read Logic) ---
    std::cout << "\n--- Testing AckMessages ---" << std::endl;
    
    // A sends message to B
    int64_t msg_id = 0;
    ASSERT_TRUE(chat_client.SaveMessage(idA, idB, "Hello B", std::time(nullptr) * 1000, msg_id), "A sends message to B");

    // Verify B has unread count
    auto sessions = chat_client.GetRecentSessions(idB);
    bool found = false;
    for (const auto& s : sessions) {
        if (s.peer_id == idA) {
            found = true;
            ASSERT_TRUE(s.unread_count > 0, "B has unread messages from A");
            break;
        }
    }
    ASSERT_TRUE(found, "Session exists for B");

    // B reads messages (Ack)
    // Note: ChatClient in tests usually connects directly to ChatService via gRPC, 
    // but AckMessages is a method we added to Gateway's ChatClient wrapper.
    // Wait, the ChatClient in `tests/functional` is likely a copy or similar to Gateway's.
    // I need to check if `tests/functional/chat_client.hpp` has AckMessages.
    // If not, I need to add it there too, or use the generated stub directly if the wrapper is missing it.
    // Let's assume I need to update the test's ChatClient wrapper as well.
    // For now, I will use the wrapper method, and if it fails to compile, I will update the wrapper.
    
    ASSERT_TRUE(chat_client.AckMessages(idB, idA), "B Acks messages from A");

    // Verify B has 0 unread count
    sessions = chat_client.GetRecentSessions(idB);
    found = false;
    for (const auto& s : sessions) {
        if (s.peer_id == idA) {
            found = true;
            ASSERT_TRUE(s.unread_count == 0, "B has 0 unread messages after Ack");
            break;
        }
    }
    ASSERT_TRUE(found, "Session exists for B after Ack");


    // --- Test 2: DeleteFriend ---
    std::cout << "\n--- Testing DeleteFriend ---" << std::endl;

    // A adds B
    std::string error_msg;
    ASSERT_TRUE(auth_client.AddFriend(idA, idB, error_msg), "A adds B as friend");
    
    // B accepts
    ASSERT_TRUE(auth_client.HandleFriendRequest(idB, idA, true, error_msg), "B accepts friend request");

    // Verify they are friends
    auto friendsA = auth_client.GetFriendList(idA);
    bool is_friend = false;
    for (const auto& f : friendsA) if (f.user_id == idB) is_friend = true;
    ASSERT_TRUE(is_friend, "B is in A's friend list");

    // A deletes B
    ASSERT_TRUE(auth_client.DeleteFriend(idA, idB, error_msg), "A deletes B");

    // Verify B is NOT in A's friend list
    friendsA = auth_client.GetFriendList(idA);
    is_friend = false;
    for (const auto& f : friendsA) if (f.user_id == idB) is_friend = true;
    ASSERT_TRUE(!is_friend, "B is NOT in A's friend list");

    // Verify A is NOT in B's friend list (Bidirectional delete)
    auto friendsB = auth_client.GetFriendList(idB);
    is_friend = false;
    for (const auto& f : friendsB) if (f.user_id == idA) is_friend = true;
    ASSERT_TRUE(!is_friend, "A is NOT in B's friend list");

    std::cout << "\nAll Feature Tests Passed!" << std::endl;
    return 0;
}
