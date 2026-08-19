#include "config_utils.hpp"

namespace Omni::Config {

std::string account_dir(const std::string& exchange) {
    return fmt::format("./account/{}", exchange);
}


std::string domain_file(const std::string& exchange) {
    return fmt::format("./domain/{}.json", exchange);
}


std::pair<bool, AuthKeys> get_auth_keys(const std::string& exchange) {
    return read_config_file<AuthKeys>(
        fmt::format("{}/auth_keys.json", account_dir(exchange))
    );
}


std::pair<bool, std::map<std::string, std::string>> get_domains(
    const std::string& exchange
) {
    return read_config_file<std::map<std::string, std::string>>(domain_file(exchange));
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
