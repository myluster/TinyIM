#pragma once
#include <hiredis/hiredis.h>
#include <string>
#include <mutex>
#include <queue>
#include <memory>
#include <optional>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <map>
#include <functional>
#include <vector>
#include "log/logger.hpp"
#include "config/config.hpp"

namespace tinyim {
namespace db {

class RedisConnection {
public:
    RedisConnection(redisContext* ctx) : ctx_(ctx) {}
    ~RedisConnection() {
        if (ctx_) {
            redisFree(ctx_);
        }
    }
    redisContext* Get() { return ctx_; }

private:
    redisContext* ctx_;
};

class RedisPool {
public:
    static RedisPool& Instance() {
        static RedisPool instance;
        return instance;
    }

    void Init(const RedisConfig& config, const std::optional<RedisSentinelConfig>& sentinel_config = std::nullopt) {
        config_ = config;

        if (sentinel_config) {
            spdlog::info("Redis Sentinel enabled. Connecting to Sentinel at {}:{}", sentinel_config->host, sentinel_config->port);
            redisContext* ctx = redisConnect(sentinel_config->host.c_str(), sentinel_config->port);
            if (ctx && !ctx->err) {
                redisReply* reply = (redisReply*)redisCommand(ctx, "SENTINEL get-master-addr-by-name %s", sentinel_config->master_name.c_str());
                if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements == 2) {
                    config_.host = reply->element[0]->str;
                    config_.port = std::stoi(reply->element[1]->str);
                    spdlog::info("Redis Master discovered at {}:{}", config_.host, config_.port);
                } else {
                    spdlog::error("Failed to get master address from Sentinel");
                }
                if (reply) freeReplyObject(reply);
                redisFree(ctx);
            } else {
                if (ctx) redisFree(ctx);
                spdlog::error("Failed to connect to Redis Sentinel");
            }
        }

        for (int i = 0; i < config.pool_size; ++i) {
            auto conn = CreateConnection();
            if (conn) {
                pool_.push(std::move(conn));
            }
        }
        spdlog::info("Redis Pool initialized with {} connections to {}:{}", pool_.size(), config_.host, config_.port);
    }

    std::shared_ptr<RedisConnection> GetConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !pool_.empty(); });

        auto conn = std::move(pool_.front());
        pool_.pop();

        // Simple ping check could be added here
        return conn;
    }

    void ReturnConnection(std::shared_ptr<RedisConnection> conn) {
        std::unique_lock<std::mutex> lock(mutex_);
        pool_.push(std::move(conn));
        cv_.notify_one();
    }

private:
    RedisPool() = default;

    std::shared_ptr<RedisConnection> CreateConnection() {
        redisContext* ctx = redisConnect(config_.host.c_str(), config_.port);
        if (!ctx || ctx->err) {
            if (ctx) {
                spdlog::error("Redis connection error: {}", ctx->errstr);
                redisFree(ctx);
            } else {
                spdlog::error("Redis connection error: can't allocate redis context");
            }
            return nullptr;
        }
        return std::make_shared<RedisConnection>(ctx);
    }

    RedisConfig config_;
    std::queue<std::shared_ptr<RedisConnection>> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

class RedisClient {
public:
    RedisClient() {
        conn_ = RedisPool::Instance().GetConnection();
    }

    ~RedisClient() {
        if (conn_) {
            RedisPool::Instance().ReturnConnection(conn_);
        }
    }

    bool Set(const std::string& key, const std::string& value) {
        if (!conn_ || !conn_->Get()) return false;
        redisReply* reply = (redisReply*)redisCommand(conn_->Get(), "SET %s %s", key.c_str(), value.c_str());
        if (!reply) return false;
        bool success = (reply->type != REDIS_REPLY_ERROR);
        freeReplyObject(reply);
        return success;
    }

    bool SetEx(const std::string& key, const std::string& value, int seconds) {
        if (!conn_ || !conn_->Get()) return false;
        redisReply* reply = (redisReply*)redisCommand(conn_->Get(), "SETEX %s %d %s", key.c_str(), seconds, value.c_str());
        if (!reply) return false;
        bool success = (reply->type != REDIS_REPLY_ERROR);
        freeReplyObject(reply);
        return success;
    }

    std::optional<std::string> Get(const std::string& key) {
        if (!conn_ || !conn_->Get()) return std::nullopt;
        redisReply* reply = (redisReply*)redisCommand(conn_->Get(), "GET %s", key.c_str());
        if (!reply) return std::nullopt;
        
        std::optional<std::string> result;
        if (reply->type == REDIS_REPLY_STRING) {
            result = reply->str;
        }
        freeReplyObject(reply);
        return result;
    }

    bool HSet(const std::string& key, const std::string& field, const std::string& value) {
        if (!conn_ || !conn_->Get()) return false;
        redisReply* reply = (redisReply*)redisCommand(conn_->Get(), "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());
        if (!reply) return false;
        bool success = (reply->type != REDIS_REPLY_ERROR);
        freeReplyObject(reply);
        return success;
    }

    std::optional<std::string> HGet(const std::string& key, const std::string& field) {
        if (!conn_ || !conn_->Get()) return std::nullopt;
        redisReply* reply = (redisReply*)redisCommand(conn_->Get(), "HGET %s %s", key.c_str(), field.c_str());
        if (!reply) return std::nullopt;
        
        std::optional<std::string> result;
        if (reply->type == REDIS_REPLY_STRING) {
            result = reply->str;
        }
        freeReplyObject(reply);
        return result;
    }

    bool Del(const std::string& key) {
        if (!conn_ || !conn_->Get()) return false;
        redisReply* reply = (redisReply*)redisCommand(conn_->Get(), "DEL %s", key.c_str());
        if (!reply) return false;
        bool success = (reply->type != REDIS_REPLY_ERROR);
        freeReplyObject(reply);
        return success;
    }

    bool HDel(const std::string& key, const std::string& field) {
        if (!conn_ || !conn_->Get()) return false;
        redisReply* reply = (redisReply*)redisCommand(conn_->Get(), "HDEL %s %s", key.c_str(), field.c_str());
        if (!reply) return false;
        bool success = (reply->type != REDIS_REPLY_ERROR);
        freeReplyObject(reply);
        return success;
    }

private:
    std::shared_ptr<RedisConnection> conn_;
};

class RedisPubSubClient {
public:
    using MessageCallback = std::function<void(const std::string& channel, const std::string& message)>;

    static RedisPubSubClient& Instance() {
        static RedisPubSubClient instance;
        return instance;
    }

    void Init(const RedisConfig& config) {
        config_ = config;
        running_ = true;
        thread_ = std::thread(&RedisPubSubClient::Loop, this);
    }

    void Publish(const std::string& channel, const std::string& message) {
        redisContext* ctx = redisConnect(config_.host.c_str(), config_.port);
        if (ctx && !ctx->err) {
            redisReply* reply = (redisReply*)redisCommand(ctx, "PUBLISH %s %s", channel.c_str(), message.c_str());
            if (reply) freeReplyObject(reply);
            redisFree(ctx);
        } else {
            if (ctx) redisFree(ctx);
            spdlog::error("Failed to publish to Redis: {}", ctx ? ctx->errstr : "Unknown error");
        }
    }

    void Subscribe(const std::string& channel, MessageCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_[channel] = callback;
        // If thread is running, we need to signal it to subscribe?
        // Hiredis blocking subscribe blocks the thread. We can't easily add subscriptions dynamically in blocking mode without a timeout or async.
        // For this simple project, we assume Subscribe is called before Init or we use a timeout loop.
        // Let's use a timeout loop in Loop().
        channels_.push_back(channel);
    }

    void Stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    RedisPubSubClient() = default;
    
    void Loop() {
        while (running_) {
            redisContext* ctx = redisConnect(config_.host.c_str(), config_.port);
            if (!ctx || ctx->err) {
                if (ctx) redisFree(ctx);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            // Subscribe to all channels
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& ch : channels_) {
                    redisReply* reply = (redisReply*)redisCommand(ctx, "SUBSCRIBE %s", ch.c_str());
                    if (reply) freeReplyObject(reply);
                }
            }

            while (running_) {
                redisReply* reply;
                if (redisGetReply(ctx, (void**)&reply) == REDIS_OK) {
                    if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {
                        std::string type = reply->element[0]->str;
                        if (type == "message") {
                            std::string channel = reply->element[1]->str;
                            std::string msg = reply->element[2]->str;
                            
                            std::lock_guard<std::mutex> lock(mutex_);
                            if (callbacks_.count(channel)) {
                                callbacks_[channel](channel, msg);
                            }
                        }
                    }
                    freeReplyObject(reply);
                } else {
                    break; // Connection lost
                }
            }
            redisFree(ctx);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    RedisConfig config_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::vector<std::string> channels_;
    std::map<std::string, MessageCallback> callbacks_;
};

} // namespace db
} // namespace tinyim
