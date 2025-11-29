#pragma once
#include <string>
#include <memory>
#include <mutex>
#include <optional>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include "log/logger.hpp"

namespace tinyim {

struct MySQLConfig {
    std::string host;
    int port;
    std::string user;
    std::string password;
    std::string dbname;
    int pool_size;

    bool operator==(const MySQLConfig& other) const {
        return host == other.host && port == other.port && 
               user == other.user && password == other.password && 
               dbname == other.dbname;
    }
};

struct RedisConfig {
    std::string host;
    int port;
    int pool_size;
};

struct RedisSentinelConfig {
    std::string host;
    int port;
    std::string master_name;
};



struct ServerConfig {
    int gateway_port;
    int auth_port;
    int chat_port;
    int status_port;
};

struct ServiceAddresses {
    std::string auth_address;
    std::string chat_address;
    std::string status_address;
};

class Config {
public:
    static Config& Instance() {
        static Config instance;
        return instance;
    }

    bool Load(const std::string& path) {
        try {
            boost::property_tree::read_json(path, pt_);
            
            // MySQL Config
            const char* env_mysql_host = std::getenv("MYSQL_HOST");
            mysql_.host = env_mysql_host ? env_mysql_host : pt_.get<std::string>("mysql.host");
            
            const char* env_mysql_port = std::getenv("MYSQL_PORT");
            mysql_.port = env_mysql_port ? std::stoi(env_mysql_port) : pt_.get<int>("mysql.port");
            
            const char* env_mysql_user = std::getenv("MYSQL_USER");
            mysql_.user = env_mysql_user ? env_mysql_user : pt_.get<std::string>("mysql.user");
            
            const char* env_mysql_pass = std::getenv("MYSQL_PASSWORD");
            mysql_.password = env_mysql_pass ? env_mysql_pass : pt_.get<std::string>("mysql.password");
            
            const char* env_mysql_db = std::getenv("MYSQL_DATABASE");
            mysql_.dbname = env_mysql_db ? env_mysql_db : pt_.get<std::string>("mysql.dbname");
            
            mysql_.pool_size = pt_.get<int>("mysql.pool_size", 5);

            // MySQL Slave Config
            if (pt_.get_child_optional("mysql_slave")) {
                const char* env_slave_host = std::getenv("MYSQL_SLAVE_HOST");
                mysql_readonly_.host = env_slave_host ? env_slave_host : pt_.get<std::string>("mysql_slave.host");
                
                const char* env_slave_port = std::getenv("MYSQL_SLAVE_PORT");
                mysql_readonly_.port = env_slave_port ? std::stoi(env_slave_port) : pt_.get<int>("mysql_slave.port");
                
                const char* env_slave_user = std::getenv("MYSQL_SLAVE_USER");
                mysql_readonly_.user = env_slave_user ? env_slave_user : pt_.get<std::string>("mysql_slave.user");
                
                const char* env_slave_pass = std::getenv("MYSQL_SLAVE_PASSWORD");
                mysql_readonly_.password = env_slave_pass ? env_slave_pass : pt_.get<std::string>("mysql_slave.password");
                
                const char* env_slave_db = std::getenv("MYSQL_SLAVE_DATABASE");
                mysql_readonly_.dbname = env_slave_db ? env_slave_db : pt_.get<std::string>("mysql_slave.dbname");
                
                mysql_readonly_.pool_size = pt_.get<int>("mysql_slave.pool_size", 5);
            } else {
                mysql_readonly_ = mysql_; 
            }

            // Redis Config
            const char* env_redis_host = std::getenv("REDIS_HOST");
            redis_.host = env_redis_host ? env_redis_host : pt_.get<std::string>("redis.host");
            
            const char* env_redis_port = std::getenv("REDIS_PORT");
            redis_.port = env_redis_port ? std::stoi(env_redis_port) : pt_.get<int>("redis.port");
            
            redis_.pool_size = pt_.get<int>("redis.pool_size", 5);

            if (pt_.get_child_optional("redis_sentinel")) {
                RedisSentinelConfig sentinel;
                sentinel.host = pt_.get<std::string>("redis_sentinel.host");
                sentinel.port = pt_.get<int>("redis_sentinel.port");
                sentinel.master_name = pt_.get<std::string>("redis_sentinel.master_name");
                redis_sentinel_ = sentinel;
            }

            // Server Config
            server_.gateway_port = pt_.get<int>("server.gateway_port");
            server_.auth_port = pt_.get<int>("server.auth_port");
            server_.chat_port = pt_.get<int>("server.chat_port");
            server_.status_port = pt_.get<int>("server.status_port", 50053);

            // Services Addresses
            const char* env_auth_addr = std::getenv("SERVICES_AUTH_ADDRESS");
            services_.auth_address = env_auth_addr ? env_auth_addr : pt_.get<std::string>("services.auth_address");
            
            const char* env_chat_addr = std::getenv("SERVICES_CHAT_ADDRESS");
            services_.chat_address = env_chat_addr ? env_chat_addr : pt_.get<std::string>("services.chat_address");
            
            const char* env_status_addr = std::getenv("SERVICES_STATUS_ADDRESS");
            services_.status_address = env_status_addr ? env_status_addr : pt_.get<std::string>("services.status_address", "localhost:50053");

            spdlog::info("Config loaded from {}", path);
            return true;
        } catch (const std::exception& e) {
            spdlog::error("Failed to load config: {}", e.what());
            return false;
        }
    }

    const MySQLConfig& MySQL() const { return mysql_; }
    const MySQLConfig& MySQLReadOnly() const { return mysql_readonly_; }
    const RedisConfig& Redis() const { return redis_; }
    const std::optional<RedisSentinelConfig>& RedisSentinel() const { return redis_sentinel_; }
    const ServerConfig& Server() const { return server_; }
    const ServiceAddresses& Services() const { return services_; }

private:
    Config() = default;
    boost::property_tree::ptree pt_;
    MySQLConfig mysql_;
    MySQLConfig mysql_readonly_;
    RedisConfig redis_;
    std::optional<RedisSentinelConfig> redis_sentinel_;
    ServerConfig server_;
    ServiceAddresses services_;
};

} // namespace tinyim
