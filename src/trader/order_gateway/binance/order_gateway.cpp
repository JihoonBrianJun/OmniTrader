#include <quill/LogMacros.h>

#include "market_listener/exchange/binance/binance_common.hpp"
#include "order_gateway.hpp"


namespace Omni::Binance {

namespace OG = Omni::OrderGateway;


BinanceOrderGateway::BinanceOrderGateway(
    const Omni::Trader::TraderConfig& config, quill::Logger* logger
)
:   logger_(logger)
{
    auto domain_type = (config.http_domain_type == "rest_test") ? "test" : "real";
    auto rest_domain = get_endpoint("rest", domain_type).second;
    auto ws_api_domain = get_endpoint("ws_api", domain_type).second;
    auto signer = make_signer();

    symbol_manager_ = std::make_shared<SymbolManager>(logger_, rest_domain);
    symbol_manager_->load();

    rest_gateway_ = std::make_unique<RestOrderGateway>(
        logger_, rest_domain, signer, symbol_manager_
    );
    if (signer && !ws_api_domain.empty()) {
        ws_gateway_ = std::make_unique<WsOrderGateway>(
            logger_, ws_api_domain, signer, symbol_manager_
        );
    }
}


void BinanceOrderGateway::place_order(
    const OG::OrderPlaceInfo& info, OG::OrderResponse& response
) {
    if (ws_usable()) {
        ws_gateway_->place_order(info, response);
        if (response.success) return;
        LOG_WARNING(logger_, "WS place failed; falling back to REST");
    }
    rest_gateway_->place_order(info, response);
}


void BinanceOrderGateway::amend_order(
    const OG::OrderAmendInfo& info, OG::OrderResponse& response
) {
    if (ws_usable()) {
        ws_gateway_->amend_order(info, response);
        if (response.success) return;
        LOG_WARNING(logger_, "WS amend failed; falling back to REST");
    }
    rest_gateway_->amend_order(info, response);
}


void BinanceOrderGateway::cancel_order(
    const OG::OrderCancelInfo& info, OG::OrderResponse& response
) {
    if (ws_usable()) {
        ws_gateway_->cancel_order(info, response);
        if (response.success) return;
        LOG_WARNING(logger_, "WS cancel failed; falling back to REST");
    }
    rest_gateway_->cancel_order(info, response);
}

} // namespace Omni::Binance
