#include <stdexcept>
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

    // Still needed with orders going out over the socket: it holds the exchange's
    // per-product filters and does the price/qty formatting the WS request needs.
    rest_client_ = std::make_shared<BinanceRestClient>(logger_, rest_domain);
    rest_client_->load();

    // Fail at startup rather than at the first order. With no REST fallback left,
    // a missing key or endpoint means this trader can never send anything, and
    // finding that out from a stream of failed placements is worse than not starting.
    if (!signer) {
        throw std::runtime_error(
            "Binance order gateway needs API credentials (account/binance/auth_keys.json)"
        );
    }
    if (ws_api_domain.empty()) {
        throw std::runtime_error(
            fmt::format("No Binance ws_api endpoint configured for domain_type \"{}\"", domain_type)
        );
    }
    ws_gateway_ = std::make_unique<WsOrderGateway>(
        logger_, ws_api_domain, signer, rest_client_
    );
}


void BinanceOrderGateway::set_response_sink(OG::IOrderGateway::ResponseSink sink) {
    if (ws_gateway_) ws_gateway_->set_response_sink(sink);
    OG::IOrderGateway::set_response_sink(std::move(sink));
}


void BinanceOrderGateway::set_product_grid(
    const std::string& product, double tick_size, double lot_size
) {
    rest_client_->set_filter(product, tick_size, lot_size);
}


bool BinanceOrderGateway::report_not_sent(uint64_t cid, const char* what) {
    LOG_WARNING(
        logger_, "Binance {} not sent: WS-API session is down (cid={})", what, cid
    );
    OG::OrderResponse response;
    response.cid = cid;
    response.success = false;
    response.msg = "Binance WS-API session unavailable";
    deliver(response);
    return false;
}


// Requests are fired without blocking; the outcome arrives later via the sink. A
// false return from the WS gateway means the request never left (session down, or
// the send threw). A request that was sent and then rejected by the venue is not
// this case -- it surfaces as a failed response on the sink instead.
bool BinanceOrderGateway::place_order(const OG::OrderPlaceInfo& info) {
    if (ws_gateway_->place_order(info)) return true;
    return report_not_sent(info.cid, "place");
}


bool BinanceOrderGateway::amend_order(const OG::OrderAmendInfo& info) {
    if (ws_gateway_->amend_order(info)) return true;
    return report_not_sent(info.cid, "amend");
}


bool BinanceOrderGateway::cancel_order(const OG::OrderCancelInfo& info) {
    if (ws_gateway_->cancel_order(info)) return true;
    return report_not_sent(info.cid, "cancel");
}

} // namespace Omni::Binance
