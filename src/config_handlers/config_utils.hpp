#pragma once
#include <string>
#include <map>
#include <utility>
#include <fstream>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

namespace Omni::Config {

// Generic JSON config loader. Returns {success, value}.
template<typename T>
std::pair<bool, T> read_config_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        fmt::println("Failed to open config file: {}", file_path);
        return std::make_pair(false, T{});
    }

    nlohmann::json j;
    try {
        file >> j;
        return std::make_pair(true, j.get<T>());
    } catch (const std::exception& e) {
        fmt::println("Failed to parse config file {}: {}", file_path, e.what());
        return std::make_pair(false, T{});
    }
}

// Base directory holding one exchange's credential/account files, e.g.
// "./config/binance".
std::string config_dir(const std::string& exchange);

// Path to one exchange's domain-endpoints file, e.g. "./domain/binance.json".
std::string domain_file(const std::string& exchange);

// Generic API credentials. Each exchange's auth_keys.json uses {"key","secret"}.
// (KIS app_key/app_secret and Binance api_key/secret_key both map onto these.)
struct AuthKeys {
    std::string key = "";
    std::string secret = "";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AuthKeys, key, secret)

std::pair<bool, AuthKeys> get_auth_keys(const std::string& exchange);

// domain/<exchange>.json is a flat {name: url} object so every exchange can
// declare whatever endpoints it needs (rest_real, ws_stream, ws_api, *_test).
std::pair<bool, std::map<std::string, std::string>> get_domains(
    const std::string& exchange
);
std::pair<bool, std::string> get_domain(
    const std::string& exchange, const std::string& domain_type
);

} // namespace Omni::Config
