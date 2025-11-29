#pragma once
#include <mysql/mysql.h>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <queue>
#include <condition_variable>
#include "log/logger.hpp"
#include "config/config.hpp"

namespace tinyim {
namespace db {

enum class Consistency {
    Strong,   // Read from Primary (Read-Your-Writes)
    Eventual  // Read from ReadOnly (Eventually Consistent)
};

class MySQLConnection {
public:
    MySQLConnection(MYSQL* conn) : conn_(conn) {}
    ~MySQLConnection() {
        if (conn_) {
            mysql_close(conn_);
        }
    }

    MYSQL* Get() { return conn_; }

private:
    MYSQL* conn_;
};

class MySQLPool {
public:
    static MySQLPool& Instance() {
        static MySQLPool instance;
        return instance;
    }

    void Init(const MySQLConfig& primary_config, const MySQLConfig& readonly_config) {
        primary_config_ = primary_config;
        readonly_config_ = readonly_config;
        
        // Automatic Mode Detection
        if (primary_config_ == readonly_config_) {
            single_node_mode_ = true;
            spdlog::info("MySQL Pool: Single Node Mode Detected. Using Primary pool for all queries.");
        } else {
            single_node_mode_ = false;
            spdlog::info("MySQL Pool: HA Mode Detected. Using separate Primary and ReadOnly pools.");
        }

        // Init Primary Pool
        for (int i = 0; i < primary_config.pool_size; ++i) {
            auto conn = CreateConnection(primary_config_);
            if (conn) {
                primary_pool_.push(std::move(conn));
            }
        }
        
        // Init ReadOnly Pool (Only if NOT in single node mode)
        if (!single_node_mode_) {
            for (int i = 0; i < readonly_config.pool_size; ++i) {
                auto conn = CreateConnection(readonly_config_);
                if (conn) {
                    readonly_pool_.push(std::move(conn));
                }
            }
        }
        
        spdlog::info("MySQL Pool initialized. Primary: {}, ReadOnly: {}", 
                     primary_pool_.size(), 
                     single_node_mode_ ? primary_pool_.size() : readonly_pool_.size());
    }

    std::shared_ptr<MySQLConnection> GetPrimaryConnection() {
        return GetConnection(primary_pool_, primary_mutex_, primary_cv_, primary_config_);
    }

    std::shared_ptr<MySQLConnection> GetReadOnlyConnection() {
        if (single_node_mode_) {
            return GetPrimaryConnection();
        }
        return GetConnection(readonly_pool_, readonly_mutex_, readonly_cv_, readonly_config_);
    }

    void ReturnPrimaryConnection(std::shared_ptr<MySQLConnection> conn) {
        ReturnConnection(conn, primary_pool_, primary_mutex_, primary_cv_);
    }

    void ReturnReadOnlyConnection(std::shared_ptr<MySQLConnection> conn) {
        if (single_node_mode_) {
            ReturnPrimaryConnection(conn);
            return;
        }
        ReturnConnection(conn, readonly_pool_, readonly_mutex_, readonly_cv_);
    }

private:
    MySQLPool() = default;

    std::shared_ptr<MySQLConnection> CreateConnection(const MySQLConfig& config) {
        MYSQL* conn = mysql_init(nullptr);
        if (!conn) {
            spdlog::error("MySQL init failed");
            return nullptr;
        }

        bool reconnect = true;
        mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

        if (!mysql_real_connect(conn, config.host.c_str(), config.user.c_str(), config.password.c_str(), config.dbname.c_str(), config.port, nullptr, 0)) {
            spdlog::error("MySQL connect failed: {}", mysql_error(conn));
            mysql_close(conn);
            return nullptr;
        }
        return std::make_shared<MySQLConnection>(conn);
    }

    std::shared_ptr<MySQLConnection> GetConnection(std::queue<std::shared_ptr<MySQLConnection>>& pool, std::mutex& mutex, std::condition_variable& cv, const MySQLConfig& config) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&pool] { return !pool.empty(); });
        
        auto conn = std::move(pool.front());
        pool.pop();
        
        if (mysql_ping(conn->Get()) != 0) {
            spdlog::warn("MySQL connection lost, reconnecting...");
            conn = CreateConnection(config);
        }

        return conn;
    }

    void ReturnConnection(std::shared_ptr<MySQLConnection> conn, std::queue<std::shared_ptr<MySQLConnection>>& pool, std::mutex& mutex, std::condition_variable& cv) {
        std::unique_lock<std::mutex> lock(mutex);
        pool.push(std::move(conn));
        cv.notify_one();
    }

    MySQLConfig primary_config_;
    MySQLConfig readonly_config_;
    bool single_node_mode_ = false;
    
    std::queue<std::shared_ptr<MySQLConnection>> primary_pool_;
    std::mutex primary_mutex_;
    std::condition_variable primary_cv_;

    std::queue<std::shared_ptr<MySQLConnection>> readonly_pool_;
    std::mutex readonly_mutex_;
    std::condition_variable readonly_cv_;
};

// RAII Wrapper for Client usage with Lazy Connection
class MySQLClient {
public:
    MySQLClient() = default;

    ~MySQLClient() {
        if (primary_conn_) {
            MySQLPool::Instance().ReturnPrimaryConnection(primary_conn_);
        }
        if (readonly_conn_) {
            MySQLPool::Instance().ReturnReadOnlyConnection(readonly_conn_);
        }
    }

    bool Execute(const std::string& query) {
        if (!EnsurePrimaryConnection()) return false;
        if (mysql_query(primary_conn_->Get(), query.c_str())) {
            spdlog::error("MySQL Execute failed: {} | Error: {}", query, mysql_error(primary_conn_->Get()));
            return false;
        }
        return true;
    }

    std::vector<std::vector<std::string>> Query(const std::string& query, Consistency consistency = Consistency::Eventual) {
        if (consistency == Consistency::Strong) {
            return QueryPrimary(query);
        } else {
            return QueryReadOnly(query);
        }
    }

    std::string Escape(const std::string& str) {
        // Prefer ReadOnly connection for escaping, but fallback to Primary
        if (readonly_conn_) {
             std::vector<char> buffer(str.length() * 2 + 1);
             mysql_real_escape_string(readonly_conn_->Get(), buffer.data(), str.c_str(), str.length());
             return std::string(buffer.data());
        }
        if (EnsurePrimaryConnection()) {
             std::vector<char> buffer(str.length() * 2 + 1);
             mysql_real_escape_string(primary_conn_->Get(), buffer.data(), str.c_str(), str.length());
             return std::string(buffer.data());
        }
        return str;
    }

    uint64_t GetLastInsertId() {
        if (primary_conn_ && primary_conn_->Get()) {
            return mysql_insert_id(primary_conn_->Get());
        }
        return 0;
    }

private:
    std::vector<std::vector<std::string>> QueryReadOnly(const std::string& query) {
        std::vector<std::vector<std::string>> results;
        if (!EnsureReadOnlyConnection()) return results;

        if (mysql_query(readonly_conn_->Get(), query.c_str())) {
            spdlog::error("MySQL QueryReadOnly failed: {} | Error: {}", query, mysql_error(readonly_conn_->Get()));
            return results;
        }

        return FetchResults(readonly_conn_->Get());
    }

    std::vector<std::vector<std::string>> QueryPrimary(const std::string& query) {
        std::vector<std::vector<std::string>> results;
        if (!EnsurePrimaryConnection()) return results;

        if (mysql_query(primary_conn_->Get(), query.c_str())) {
            spdlog::error("MySQL QueryPrimary failed: {} | Error: {}", query, mysql_error(primary_conn_->Get()));
            return results;
        }

        return FetchResults(primary_conn_->Get());
    }

    bool EnsurePrimaryConnection() {
        if (!primary_conn_) {
            primary_conn_ = MySQLPool::Instance().GetPrimaryConnection();
        }
        return primary_conn_ != nullptr;
    }

    bool EnsureReadOnlyConnection() {
        if (!readonly_conn_) {
            readonly_conn_ = MySQLPool::Instance().GetReadOnlyConnection();
        }
        return readonly_conn_ != nullptr;
    }

    std::shared_ptr<MySQLConnection> primary_conn_;
    std::shared_ptr<MySQLConnection> readonly_conn_;

    std::vector<std::vector<std::string>> FetchResults(MYSQL* conn) {
        std::vector<std::vector<std::string>> results;
        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) return results;

        int num_fields = mysql_num_fields(res);
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            std::vector<std::string> row_data;
            for (int i = 0; i < num_fields; i++) {
                row_data.push_back(row[i] ? row[i] : "NULL");
            }
            results.push_back(row_data);
        }
        mysql_free_result(res);
        return results;
    }
};

} // namespace db
} // namespace tinyim

