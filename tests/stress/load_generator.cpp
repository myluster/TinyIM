#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <grpcpp/grpcpp.h>
#include "auth_client.hpp"
#include "config/config.hpp"
#include "log/logger.hpp"

std::atomic<int> success_count{0};
std::atomic<int> fail_count{0};

void worker(int id, int iterations, std::string auth_address) {
    auto channel = grpc::CreateChannel(auth_address, grpc::InsecureChannelCredentials());
    AuthClient client(channel);

    for (int i = 0; i < iterations; ++i) {
        std::string username = "stress_" + std::to_string(id) + "_" + std::to_string(i) + "_" + std::to_string(std::time(nullptr));
        std::string password = "password";
        int64_t user_id = 0;
        
        // Register
        if (client.Register(username, password, user_id)) {
            // Login
            std::string token;
            int64_t login_uid = 0;
            if (client.Login(username, password, token, login_uid)) {
                success_count++;
            } else {
                fail_count++;
            }
        } else {
            fail_count++;
        }
    }
}

int main(int argc, char* argv[]) {
    tinyim::Logger::Init();
    if (!tinyim::Config::Instance().Load("configs/config.json")) {
        std::cerr << "Failed to load config" << std::endl;
        return 1;
    }

    std::string auth_address = tinyim::Config::Instance().Services().auth_address;
    
    int threads = 10;
    int iterations = 100;

    if (argc > 1) threads = std::stoi(argv[1]);
    if (argc > 2) iterations = std::stoi(argv[2]);

    std::cout << "Starting Stress Test with " << threads << " threads, " << iterations << " iterations each." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> workers;
    for (int i = 0; i < threads; ++i) {
        workers.emplace_back(worker, i, iterations, auth_address);
    }

    for (auto& t : workers) {
        t.join();
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << "Test Finished in " << elapsed.count() << " seconds." << std::endl;
    std::cout << "Total Requests (Login): " << (success_count + fail_count) << std::endl;
    std::cout << "Success: " << success_count << std::endl;
    std::cout << "Failed: " << fail_count << std::endl;
    std::cout << "RPS: " << (success_count / elapsed.count()) << std::endl;

    return 0;
}
