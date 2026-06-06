#pragma once
#include <string>
#include <utility>
#include <nlohmann/json.hpp>
#include <quill/Logger.h>

// KIS-specific configuration: account identifiers, REST/websocket session tokens
// and the websocket approval key. Generic credentials (app_key/app_secret) and
// domains are read via Omni::Config with exchange "kis".

namespace Omni::KIS::Config {

constexpr char const* EXCHANGE = "kis";

struct AccountInfo {
    std::string front = "";
    std::string rear = "";
    std::string hts_id = "";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AccountInfo, front, rear, hts_id)
std::pair<bool, AccountInfo> get_account_info(
    const std::string& account_info_path = "./config/kis/account_info.json"
);

struct RestAccessResponse {
    std::string access_token = "";
    std::string access_token_token_expired = "";
    std::string token_type = "";
    long expires_in = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
    RestAccessResponse, access_token, access_token_token_expired, token_type, expires_in
)

struct RestAccessConfig {
    std::string access_token = "";
    long access_token_expire_time = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RestAccessConfig, access_token, access_token_expire_time)
std::pair<bool, RestAccessConfig> get_rest_access_config(
    const std::string& rest_token_path = "./config/kis/rest_access.json"
);
std::pair<bool, std::string> get_rest_access_token(
    const std::string& rest_token_path = "./config/kis/rest_access.json"
);

struct WebsocketAccessResponse {
    std::string approval_key = "";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WebsocketAccessResponse, approval_key)

struct WebsocketAccessConfig {
    std::string access_token = "";
    long access_token_expire_time = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WebsocketAccessConfig, access_token, access_token_expire_time)
std::pair<bool, WebsocketAccessConfig> get_websocket_access_config(
    const std::string& websocket_token_path = "./config/kis/websocket_access.json"
);
std::pair<bool, std::string> get_websocket_access_token(
    const std::string& websocket_token_path = "./config/kis/websocket_access.json"
);

// Refresh REST + websocket session tokens if expired (writes config files).
void update_access_tokens(quill::Logger* logger);

} // namespace Omni::KIS::Config
