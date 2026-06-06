#include <chrono>
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
    std::shared_ptr<SymbolManager> symbol_manager
)
:   logger_(logger),
    ws_api_domain_(ws_api_domain),
    signer_(std::move(signer)),
    symbol_manager_(std::move(symbol_manager)),
    connected_(false),
    request_counter_(0)
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
    try {
        auto j = nlohmann::json::parse(payload);
        if (!j.contains("id")) return;
        std::string id = j.at("id").get<std::string>();

        OG::OrderResponse resp;
        int status = j.value("status", 0);
        if (status == 200 && j.contains("result")) {
            resp.success = true;
            const auto& result = j.at("result");
            if (result.contains("orderId")) {
                resp.order_no = std::to_string(result.at("orderId").get<long>());
            }
        } else {
            resp.success = false;
            if (j.contains("error")) resp.msg = j.at("error").dump();
        }

        {
            std::lock_guard<std::mutex> lk(mtx_);
            results_[id] = resp;
            ready_[id] = true;
        }
        cv_.notify_all();
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Failed to parse WS-API response: {}", e.what());
    }
}


OG::OrderResponse WsOrderGateway::send_request(
    const std::string& method, std::map<std::string, std::string> params
) {
    OG::OrderResponse response;
    if (!connected_.load() || !signer_) {
        response.success = false;
        return response;
    }

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

    auto id = std::to_string(++request_counter_);
    nlohmann::json req;
    req["id"] = id;
    req["method"] = method;
    req["params"] = params;   // all string values
    std::string payload = req.dump();

    {
        std::lock_guard<std::mutex> lk(mtx_);
        results_[id] = OG::OrderResponse{};
        ready_[id] = false;
    }

    try {
        client_->send(client_->get_connection_hdl(), payload, websocketpp::frame::opcode::TEXT);
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "WS-API send failed: {}", e.what());
        std::lock_guard<std::mutex> lk(mtx_);
        results_.erase(id);
        ready_.erase(id);
        response.success = false;
        return response;
    }

    std::unique_lock<std::mutex> lk(mtx_);
    bool got = cv_.wait_for(lk, std::chrono::seconds(3), [&] { return ready_[id]; });
    if (got) {
        response = results_[id];
    } else {
        LOG_WARNING(logger_, "WS-API request {} timed out", id);
        response.success = false;
    }
    results_.erase(id);
    ready_.erase(id);
    return response;
}


void WsOrderGateway::place_order(const OG::OrderPlaceInfo& info, OG::OrderResponse& response) {
    std::map<std::string, std::string> params{
        {"symbol", info.code},
        {"side", info.is_bid ? "BUY" : "SELL"},
        {"type", info.is_limit ? "LIMIT" : "MARKET"},
        {"quantity", symbol_manager_->format_qty(info.code, info.qty)}
    };
    if (info.is_limit) {
        params["timeInForce"] = "GTC";
        params["price"] = symbol_manager_->format_price(info.code, info.price);
    }
    response = send_request("order.place", std::move(params));
}


void WsOrderGateway::amend_order(const OG::OrderAmendInfo& info, OG::OrderResponse& response) {
    std::map<std::string, std::string> params{
        {"symbol", info.code},
        {"orderId", info.order_no},
        {"quantity", symbol_manager_->format_qty(info.code, info.qty)},
        {"price", symbol_manager_->format_price(info.code, info.price)}
    };
    response = send_request("order.modify", std::move(params));
}


void WsOrderGateway::cancel_order(const OG::OrderCancelInfo& info, OG::OrderResponse& response) {
    std::map<std::string, std::string> params{
        {"symbol", info.code},
        {"orderId", info.order_no}
    };
    response = send_request("order.cancel", std::move(params));
}

} // namespace Omni::Binance
