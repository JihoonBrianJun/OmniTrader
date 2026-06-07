#include <cmath>
#include <sstream>
#include <iomanip>
#include <fmt/core.h>
#include <quill/LogMacros.h>
#include <nlohmann/json.hpp>

#include "connection_handlers/http/http_client.hpp"
#include "product_manager.hpp"


namespace Omni::Binance {

static int precision_from_step(double step) {
    if (step <= 0.0) return 8;
    int p = 0;
    double s = step;
    while (s < 1.0 - 1e-12 && p < 12) {
        s *= 10.0;
        ++p;
    }
    return p;
}


ProductManager::ProductManager(quill::Logger* logger, const std::string& rest_domain)
:   logger_(logger),
    rest_domain_(rest_domain)
{
}


void ProductManager::load() {
    auto client = std::make_shared<Omni::Connection::HttpClient>(logger_);
    auto response = client->get(fmt::format("{}/fapi/v1/exchangeInfo", rest_domain_));
    if (!response.success || response.status_code != 200) {
        LOG_WARNING(
            logger_, "Failed to load exchangeInfo: {} {} {}",
            response.error_msg, response.status_code, response.body
        );
        return;
    }

    try {
        auto j = nlohmann::json::parse(response.body);
        for (const auto& sym : j.at("symbols")) {   // Binance wire key (do not rename)
            ProductFilter f;
            std::string product = sym.at("symbol").get<std::string>();
            for (const auto& filt : sym.at("filters")) {
                auto type = filt.at("filterType").get<std::string>();
                if (type == "PRICE_FILTER") {
                    f.tick_size = std::stod(filt.at("tickSize").get<std::string>());
                } else if (type == "LOT_SIZE") {
                    f.step_size = std::stod(filt.at("stepSize").get<std::string>());
                }
            }
            f.price_precision = precision_from_step(f.tick_size);
            f.qty_precision = precision_from_step(f.step_size);
            filters_[product] = f;
        }
        LOG_INFO(logger_, "Loaded {} Binance product filters", filters_.size());
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Failed to parse exchangeInfo: {}", e.what());
    }
}


bool ProductManager::has(const std::string& product) const {
    return filters_.find(product) != filters_.end();
}


ProductFilter ProductManager::filter(const std::string& product) const {
    auto it = filters_.find(product);
    if (it != filters_.end()) return it->second;
    return ProductFilter{};
}


double ProductManager::round_price(const std::string& product, double price) const {
    auto f = filter(product);
    if (f.tick_size <= 0.0) return price;
    return std::round(price / f.tick_size) * f.tick_size;
}


double ProductManager::round_qty(const std::string& product, double qty) const {
    auto f = filter(product);
    if (f.step_size <= 0.0) return qty;
    return std::floor(qty / f.step_size) * f.step_size;
}


std::string ProductManager::format_price(const std::string& product, double price) const {
    auto f = filter(product);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(f.price_precision) << round_price(product, price);
    return oss.str();
}


std::string ProductManager::format_qty(const std::string& product, double qty) const {
    auto f = filter(product);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(f.qty_precision) << round_qty(product, qty);
    return oss.str();
}

} // namespace Omni::Binance
