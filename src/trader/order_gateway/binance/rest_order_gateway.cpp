#include <fmt/core.h>
#include <quill/LogMacros.h>
#include <nlohmann/json.hpp>

#include "utils/datetime.hpp"
#include "connection_handlers/rest/rest_client.hpp"
#include "market_listener/exchange/binance/binance_common.hpp"
#include "rest_order_gateway.hpp"


namespace Omni::Binance {

RestOrderGateway::RestOrderGateway(
    quill::Logger* logger,
    const std::string& rest_domain,
    std::shared_ptr<Omni::Config::ISigner> signer,
    std::shared_ptr<BinanceRestClient> rest_client
)
:   logger_(logger),
    rest_domain_(rest_domain),
    signer_(std::move(signer)),
    rest_client_(std::move(rest_client))
{
}


void RestOrderGateway::send_order(
    const std::string& method, const std::string& params, OG::OrderResponse& response
) {
    if (!signer_) {
        LOG_WARNING(logger_, "No signer for Binance REST order");
        return;
    }
    auto full_params = fmt::format(
        "{}&timestamp={}&recvWindow={}", params, get_curr_tstamp_ms(), DEFAULT_RECV_WINDOW
    );
    auto url = fmt::format("{}/fapi/v1/order?{}", rest_domain_, sign_query(*signer_, full_params));

    auto client = std::make_shared<Omni::Connection::RestClient>(logger_);
    Omni::Connection::RestResponse http;
    if (method == "POST") http = client->post(url, "", auth_header(*signer_));
    else if (method == "PUT") http = client->put(url, "", auth_header(*signer_));
    else http = client->del(url, auth_header(*signer_));

    if (!http.success || http.status_code != 200) {
        LOG_WARNING(
            logger_, "Binance REST {} order failed: {} {} {}",
            method, http.error_msg, http.status_code, http.body
        );
        response.success = false;
        response.msg = http.body;
        return;
    }

    try {
        auto j = nlohmann::json::parse(http.body);
        response.success = true;
        if (j.contains("orderId")) response.order_no = std::to_string(j.at("orderId").get<long>());
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Failed to parse Binance order response: {}", e.what());
    }
}


void RestOrderGateway::place_order(const OG::OrderPlaceInfo& info, OG::OrderResponse& response) {
    std::string params = fmt::format(
        "symbol={}&side={}&type={}&quantity={}",
        info.product, info.is_bid ? "BUY" : "SELL", info.is_limit ? "LIMIT" : "MARKET",
        rest_client_->format_qty(info.product, info.qty)
    );
    if (info.is_limit) {
        params += fmt::format(
            "&timeInForce=GTC&price={}", rest_client_->format_price(info.product, info.price)
        );
    }
    send_order("POST", params, response);
}


void RestOrderGateway::amend_order(const OG::OrderAmendInfo& info, OG::OrderResponse& response) {
    // Binance amend (PUT /fapi/v1/order) requires side, quantity and price.
    std::string params = fmt::format(
        "symbol={}&orderId={}&quantity={}&price={}",
        info.product, info.order_no,
        rest_client_->format_qty(info.product, info.qty),
        rest_client_->format_price(info.product, info.price)
    );
    send_order("PUT", params, response);
}


void RestOrderGateway::cancel_order(const OG::OrderCancelInfo& info, OG::OrderResponse& response) {
    std::string params = fmt::format("symbol={}&orderId={}", info.product, info.order_no);
    send_order("DELETE", params, response);
}

} // namespace Omni::Binance
