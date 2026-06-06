#pragma once
#include <string>
#include <map>
#include <memory>
#include <utility>

#include <fmt/core.h>

#include "config_handlers/config_utils.hpp"
#include "config_handlers/signer.hpp"

// Shared Binance helpers: exchange id, domain selection, and HMAC signing of
// REST/WS-API requests.

namespace Omni::Binance {

constexpr char const* EXCHANGE = "binance";
constexpr long DEFAULT_RECV_WINDOW = 5000;

// domain_type is "real" or "test"; key is one of: rest, ws_stream, ws_api.
inline std::pair<bool, std::string> get_endpoint(
    const std::string& key, const std::string& domain_type
) {
    return Omni::Config::get_domain(EXCHANGE, fmt::format("{}_{}", key, domain_type));
}

inline std::shared_ptr<Omni::Config::HmacSha256Signer> make_signer() {
    auto keys = Omni::Config::get_auth_keys(EXCHANGE);
    if (!keys.first) return nullptr;
    return std::make_shared<Omni::Config::HmacSha256Signer>(keys.second.key, keys.second.secret);
}

inline std::map<std::string, std::string> auth_header(const Omni::Config::ISigner& signer) {
    return {{"X-MBX-APIKEY", signer.api_key()}};
}

// Appends the HMAC signature to a query string (timestamp/recvWindow already in).
inline std::string sign_query(const Omni::Config::ISigner& signer, const std::string& query) {
    return fmt::format("{}&signature={}", query, signer.sign(query));
}

} // namespace Omni::Binance
