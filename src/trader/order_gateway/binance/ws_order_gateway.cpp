#include <fmt/core.h>
#include <quill/LogMacros.h>
#include <nlohmann/json.hpp>
#include <websocketpp/frame.hpp>

#include "utils/datetime.hpp"
#include "market_listener/exchange/binance/binance_common.hpp"
#include "ws_order_gateway.hpp"


namespace Omni::Binance {

WsOrderGateway::WsOrderGateway(
    quill::Logger* logger,
    const std::string& ws_api_domain,
    std::shared_ptr<Omni::Config::ISigner> signer,
    std::shared_ptr<BinanceRestClient> rest_client
)
:   logger_(logger),
    ws_api_domain_(ws_api_domain),
    signer_(std::move(signer)),
    rest_client_(std::move(rest_client)),
    connected_(false)
{
    auto status_cb = [this](bool /*connecting*/, bool opened) {
        connected_.store(opened);
    };
    auto message_cb = [this](const std::string& payload) { on_message(payload); };
    client_ = std::make_unique<BinanceWebsocketClient>(
        ws_api_domain_, logger_, status_cb, message_cb
    );
}


WsOrderGateway::~WsOrderGateway() {
    client_.reset();
}


void WsOrderGateway::on_message(const std::string& payload) {
    // Parse the WS-API reply and hand it to the trader via the response sink. The
    // request id was set to the order's cid, so both success and error replies
    // correlate back by cid (the error arm carries no result, hence no orderId).
    try {
        auto j = nlohmann::json::parse(payload);
        if (!j.contains("id")) return;

        OG::OrderResponse response;
        response.cid = static_cast<uint32_t>(std::stoul(j.at("id").get<std::string>()));

        int status = j.value("status", 0);
        if (status == 200 && j.contains("result")) {
            response.success = true;
            const auto& result = j.at("result");
            if (result.contains("orderId")) {
                response.order_no = std::to_string(result.at("orderId").get<long>());
            }
        } else {
            response.success = false;
            if (j.contains("error")) response.msg = j.at("error").dump();
        }

        deliver(response);
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Failed to parse WS-API response: {}", e.what());
    }
}


bool WsOrderGateway::send_request(
    const std::string& method, std::map<std::string, std::string> params, uint32_t cid
) {
    if (!connected_.load() || !signer_) return false;

    // WS-API signature: sign the alphabetically-sorted query string of all
    // params (apiKey/timestamp/recvWindow included). std::map is already sorted.
    params["apiKey"] = signer_->api_key();
    params["recvWindow"] = std::to_string(DEFAULT_RECV_WINDOW);
    params["timestamp"] = std::to_string(get_curr_tstamp_ms());

    std::string query;
    for (const auto& [k, v] : params) {
        if (!query.empty()) query += "&";
        query += fmt::format("{}={}", k, v);
    }
    params["signature"] = signer_->sign(query);

    nlohmann::json req;
    req["id"] = std::to_string(cid);   // reply correlates back by cid
    req["method"] = method;
    req["params"] = params;            // all string values
    std::string payload = req.dump();

    try {
        client_->send(client_->get_connection_hdl(), payload, websocketpp::frame::opcode::TEXT);
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "WS-API send failed: {}", e.what());
        return false;
    }
    return true;
}


bool WsOrderGateway::place_order(const OG::OrderPlaceInfo& info) {
    std::map<std::string, std::string> params{
        {"symbol", info.product},
        {"side", info.is_bid ? "BUY" : "SELL"},
        {"type", info.is_limit ? "LIMIT" : "MARKET"},
        {"quantity", rest_client_->format_qty(info.product, info.qty)},
        {"newClientOrderId", std::to_string(info.cid)}
    };
    if (info.is_limit) {
        params["timeInForce"] = "GTC";
        params["price"] = rest_client_->format_price(info.product, info.price);
    }
    return send_request("order.place", std::move(params), info.cid);
}


bool WsOrderGateway::amend_order(const OG::OrderAmendInfo& info) {
    std::map<std::string, std::string> params{
        {"symbol", info.product},
        {"orderId", info.order_no},
        {"quantity", rest_client_->format_qty(info.product, info.qty)},
        {"price", rest_client_->format_price(info.product, info.price)}
    };
    return send_request("order.modify", std::move(params), info.cid);
}


bool WsOrderGateway::cancel_order(const OG::OrderCancelInfo& info) {
    std::map<std::string, std::string> params{
        {"symbol", info.product},
        {"orderId", info.order_no}
    };
    return send_request("order.cancel", std::move(params), info.cid);
}

} // namespace Omni::Binance
