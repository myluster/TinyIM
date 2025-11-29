#pragma once
#include <string>
#include <vector>
#include <random>
#include <sstream>
#include <iomanip>
#include <fmt/format.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

namespace tinyim {
namespace utils {

class Password {
public:
    static std::string GenerateSalt(size_t length = 16) {
        std::vector<unsigned char> buffer(length);
        if (RAND_bytes(buffer.data(), length) != 1) {
            // Fallback to std::random_device if OpenSSL fails (unlikely)
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_int_distribution<> dis(0, 255);
            for (size_t i = 0; i < length; ++i) buffer[i] = static_cast<unsigned char>(dis(gen));
        }
        return BytesToHex(buffer);
    }

    static std::string Hash(const std::string& password, const std::string& salt) {
        std::string salted_password = password + salt;
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(salted_password.c_str()), salted_password.length(), hash);
        return BytesToHex(std::vector<unsigned char>(hash, hash + SHA256_DIGEST_LENGTH));
    }

    static bool Verify(const std::string& password, const std::string& hash, const std::string& salt) {
        return Hash(password, salt) == hash;
    }

private:
    static std::string BytesToHex(const std::vector<unsigned char>& bytes) {
        std::string hex;
        hex.reserve(bytes.size() * 2);
        for (unsigned char b : bytes) {
            hex += fmt::format("{:02x}", b);
        }
        return hex;
    }
};

} // namespace utils
} // namespace tinyim
