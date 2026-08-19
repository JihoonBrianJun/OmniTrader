#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

#include <fmt/core.h>
#include <quill/LogMacros.h>
#include <nlohmann/json.hpp>

#include "utils/datetime.hpp"
#include "config_handlers/config_utils.hpp"
#include "connection_handlers/rest/rest_client.hpp"
#include "kis_config.hpp"


namespace Omni::KIS::Config {

namespace OConf = Omni::Config;


std::pair<bool, AccountInfo> get_account_info(const std::string& account_info_path) {
    return OConf::read_config_file<AccountInfo>(account_info_path);
}


std::pair<bool, RestAccessConfig> get_rest_access_config(const std::string& rest_token_path) {
    return OConf::read_config_file<RestAccessConfig>(rest_token_path);
}


std::pair<bool, std::string> get_rest_access_token(const std::string& rest_token_path) {
    auto cfg = get_rest_access_config(rest_token_path);
    return std::make_pair(cfg.first, cfg.second.access_token);
}


std::pair<bool, WebsocketAccessConfig> get_websocket_access_config(
    const std::string& websocket_token_path
) {
    return OConf::read_config_file<WebsocketAccessConfig>(websocket_token_path);
}


std::pair<bool, std::string> get_websocket_access_token(const std::string& websocket_token_path) {
    auto cfg = get_websocket_access_config(websocket_token_path);
    return std::make_pair(cfg.first, cfg.second.access_token);
}


static long parse_datetime(const std::string& datetime_str, int timezone_offset_hours = 9) {
    std::tm tm = {};
    std::istringstream ss(datetime_str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    time_t epoch = timegm(&tm);
    return epoch - timezone_offset_hours * 3600;
}


static bool is_token_expired(long token_expire_time) {
    return (token_expire_time < get_curr_tstamp_sec());
}


static void update_rest_access_token(const std::string& token_save_path, quill::Logger* logger) {
    auto rest_access_config = get_rest_access_config();
    bool new_token_needed = rest_access_config.first
        ? is_token_expired(rest_access_config.second.access_token_expire_time)
        : true;
    if (!new_token_needed) return;

    auto auth_keys = OConf::get_auth_keys(EXCHANGE);
    auto auth_domain = OConf::get_domain(EXCHANGE, "rest_real");
    if (!auth_keys.first || !auth_domain.first) return;

    nlohmann::json request_data = {
        {"grant_type", "client_credentials"},
        {"appkey", auth_keys.second.key},
        {"appsecret", auth_keys.second.secret}
    };

    auto client = std::make_shared<Omni::Connection::RestClient>(logger);
    auto response = client->post(
        fmt::format("{}/oauth2/tokenP", auth_domain.second), request_data.dump()
    );

    if (!response.success || (response.status_code != 200)) {
        LOG_WARNING(
            logger, "Unsuccessful rest token response: {} {} {}",
            response.error_msg, response.status_code, response.body
        );
        return;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(response.body);
        auto resp = j.get<RestAccessResponse>();
        auto cfg = RestAccessConfig{
            resp.access_token, parse_datetime(resp.access_token_token_expired)
        };
        std::ofstream file(token_save_path);
        file << nlohmann::json(cfg).dump(4);
    } catch (const std::exception& e) {
        LOG_WARNING(logger, "Failed to parse rest token response body: {}", response.body);
    }
}


static void update_websocket_access_token(const std::string& token_save_path, quill::Logger* logger) {
    auto ws_access_config = get_websocket_access_config();
    bool new_token_needed = ws_access_config.first
        ? is_token_expired(ws_access_config.second.access_token_expire_time)
        : true;
    if (!new_token_needed) return;

    auto auth_keys = OConf::get_auth_keys(EXCHANGE);
    auto auth_domain = OConf::get_domain(EXCHANGE, "rest_real");
    if (!auth_keys.first || !auth_domain.first) return;

    nlohmann::json request_data = {
        {"grant_type", "client_credentials"},
        {"appkey", auth_keys.second.key},
        {"secretkey", auth_keys.second.secret}
    };

    auto client = std::make_shared<Omni::Connection::RestClient>(logger);
    auto response = client->post(
        fmt::format("{}/oauth2/Approval", auth_domain.second), request_data.dump()
    );

    if (!response.success || (response.status_code != 200)) {
        LOG_WARNING(
            logger, "Unsuccessful ws approval response: {} {} {}",
            response.error_msg, response.status_code, response.body
        );
        return;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(response.body);
        auto resp = j.get<WebsocketAccessResponse>();
        auto cfg = WebsocketAccessConfig{
            resp.approval_key, get_curr_tstamp_sec() + 24 * 3600
        };
        std::ofstream file(token_save_path);
        file << nlohmann::json(cfg).dump(4);
    } catch (const std::exception& e) {
        LOG_WARNING(logger, "Failed to parse ws approval response body: {}", response.body);
    }
}


void update_access_tokens(quill::Logger* logger) {
    update_rest_access_token("./account/kis/rest_access.json", logger);
    update_websocket_access_token("./account/kis/websocket_access.json", logger);
}

} // namespace Omni::KIS::Config
