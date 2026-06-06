#include "config_utils.hpp"

namespace Omni::Config {

std::string config_dir(const std::string& exchange) {
    return fmt::format("./config/{}", exchange);
}


std::pair<bool, AuthKeys> get_auth_keys(const std::string& exchange) {
    return read_config_file<AuthKeys>(
        fmt::format("{}/auth_keys.json", config_dir(exchange))
    );
}


std::pair<bool, std::map<std::string, std::string>> get_domains(
    const std::string& exchange
) {
    return read_config_file<std::map<std::string, std::string>>(
        fmt::format("{}/domain_config.json", config_dir(exchange))
    );
}


std::pair<bool, std::string> get_domain(
    const std::string& exchange, const std::string& domain_type
) {
    auto domains = get_domains(exchange);
    if (!domains.first) {
        return std::make_pair(false, "");
    }

    auto it = domains.second.find(domain_type);
    if (it == domains.second.end()) {
        fmt::println("Unknown domain type '{}' for exchange '{}'", domain_type, exchange);
        return std::make_pair(false, "");
    }
    return std::make_pair(true, it->second);
}

} // namespace Omni::Config
