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
    const auto& domain_type = config.domain_type;
    auto rest_domain = get_endpoint("rest", domain_type).second;
    auto ws_api_domain = get_endpoint("ws_api", domain_type).second;
    auto signer = make_signer();

    rest_client_ = std::make_shared<BinanceRestClient>(logger_, rest_domain);
    rest_client_->load();

    rest_gateway_ = std::make_unique<RestOrderGateway>(
        logger_, rest_domain, signer, rest_client_
    );
    if (signer && !ws_api_domain.empty()) {
        ws_gateway_ = std::make_unique<WsOrderGateway>(
            logger_, ws_api_domain, signer, rest_client_
        );
    }
}


void BinanceOrderGateway::set_response_sink(OG::IOrderGateway::ResponseSink sink) {
    if (ws_gateway_) ws_gateway_->set_response_sink(sink);
    if (rest_gateway_) rest_gateway_->set_response_sink(sink);
}


// Requests are fired without blocking; the outcome arrives later via the sink. The
// WS gateway returns false only when it couldn't send (session down / send threw),
// in which case we fall back to REST. A WS request that was sent but later rejected
// by the exchange surfaces as a failed response on the sink (no REST retry).
bool BinanceOrderGateway::place_order(const OG::OrderPlaceInfo& info) {
    if (ws_usable() && ws_gateway_->place_order(info)) return true;
    return rest_gateway_->place_order(info);
}


bool BinanceOrderGateway::amend_order(const OG::OrderAmendInfo& info) {
    if (ws_usable() && ws_gateway_->amend_order(info)) return true;
    return rest_gateway_->amend_order(info);
}


bool BinanceOrderGateway::cancel_order(const OG::OrderCancelInfo& info) {
    if (ws_usable() && ws_gateway_->cancel_order(info)) return true;
    return rest_gateway_->cancel_order(info);
}

} // namespace Omni::Binance
