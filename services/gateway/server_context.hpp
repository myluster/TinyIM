#pragma once
#include <memory>
#include <boost/asio/thread_pool.hpp>
#include "auth_client.hpp"
#include "chat_client.hpp"
#include "status_client.hpp"

class SessionManager;

struct ServerContext {
    std::shared_ptr<AuthClient> auth_client;
    std::shared_ptr<ChatClient> chat_client;
    std::shared_ptr<StatusClient> status_client;
    std::shared_ptr<SessionManager> session_manager;
    std::shared_ptr<boost::asio::thread_pool> thread_pool;
};
