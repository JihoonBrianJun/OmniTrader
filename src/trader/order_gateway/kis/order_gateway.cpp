#include <stdexcept>
#include <fmt/core.h>

#include "config_handlers/config_utils.hpp"
#include "config_handlers/kis/kis_config.hpp"
#include "trader/order_gateway/kis/korean_stock_order_gateway.hpp"
#include "trader/order_gateway/kis/order_gateway.hpp"


namespace Omni::KIS {

std::unique_ptr<Omni::OrderGateway::IOrderGateway> create_kis_order_gateway(
    const Omni::Trader::TraderConfig& config, quill::Logger* logger
) {
    auto http_domain = Omni::Config::get_domain(Config::EXCHANGE, config.http_domain_type).second;

    if (config.market_type == "stock") {
        return std::make_unique<OrderGateway::KoreanStockOrderGateway>(logger, http_domain);
    }
    throw std::runtime_error(fmt::format(
        "KIS order gateway not supported for market type {}", config.market_type
    ));
}

} // namespace Omni::KIS
