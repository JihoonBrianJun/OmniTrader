#include <algorithm>
#include <fmt/core.h>
#include <quill/LogMacros.h>
#include <websocketpp/frame.hpp>

#include "utils/datetime.hpp"
#include "market_listener/exchange/binance/binance_common.hpp"
#include "order_dtypes.hpp"
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
    connected_(false),
    running_(true),
    reconnect_pending_(false),
    io_context_(std::make_unique<boost::asio::io_context>())
{
    io_thread_ = std::thread([this]() {
        auto work_guard = boost::asio::make_work_guard(*io_context_);
        try {
            io_context_->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger_, "Exception within WS-API gateway io_context thread: {}", e.what());
        }
    });
    create_client();
}


WsOrderGateway::~WsOrderGateway() {
    // Stop before tearing anything down, so a status callback landing mid-teardown
    // does not schedule a redial into a dying object.
    running_.store(false);
    // Stopping the context is what cancels a pending redial: reconnect_timer_ is
    // owned by io_thread_, so touching it from here would race with the redial that
    // is being torn down. Nothing is left to run once the thread is joined.
    if (io_context_) io_context_->stop();
    if (io_thread_.joinable()) io_thread_.join();
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_.reset();
    }
}


void WsOrderGateway::create_client() {
    // Ping more often than the streams do (180s): this session is how every order
    // and every shutdown cancel reaches the venue, so a dead one has to be noticed
    // in tens of seconds rather than minutes.
    constexpr int WS_API_PING_INTERVAL_SEC = 60;

    auto status_cb = [this](bool connecting, bool opened) {
        connected_.store(opened);
        if (opened) {
            LOG_INFO(logger_, "Binance WS-API order session up");
            boost::asio::post(*io_context_, [this]() { reconnect_cnt_ = 0; });
            return;
        }
        // Still dialling: not a failure yet, the open/fail handler will say which.
        if (connecting || !running_.load()) return;
        // One redial in flight at a time -- the close and fail handlers can both
        // report the same drop.
        if (reconnect_pending_.exchange(true)) return;
        boost::asio::post(*io_context_, [this]() { schedule_reconnect(); });
    };
    auto message_cb = [this](const std::string& payload) { on_message(payload); };
    // Held across the construction: the client starts its socket thread from its own
    // constructor, so it can report a failed dial -- and have io_thread_ come here to
    // replace it -- before this assignment has even happened.
    auto client = std::make_unique<BinanceWebsocketClient>(
        ws_api_domain_, logger_, status_cb, message_cb, WS_API_PING_INTERVAL_SEC
    );
    std::lock_guard<std::mutex> lock(client_mutex_);
    client_ = std::move(client);
}


void WsOrderGateway::schedule_reconnect() {
    // On io_thread_, never on the socket's own thread: this destroys the client, and
    // its destructor joins the thread the status callback runs on.
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_.reset();
    }
    connected_.store(false);

    // Capped exponential backoff, as the listener's sockets use: fast enough that a
    // brief drop costs one decision tick, bounded so a venue-side outage is not
    // hammered.
    reconnect_timer_ = std::make_unique<boost::asio::steady_timer>(*io_context_);
    reconnect_timer_->expires_after(std::chrono::seconds(
        std::min(1 << std::min(reconnect_cnt_++, 5), 30)
    ));
    reconnect_timer_->async_wait([this](const boost::system::error_code ec) {
        reconnect_pending_.store(false);
        if (ec || !running_.load()) return;   // cancelled on shutdown
        LOG_INFO(
            logger_, "Reconnecting Binance WS-API order session (attempt {})", reconnect_cnt_
        );
        create_client();
    });
}


void WsOrderGateway::on_message(const std::string& payload) {
    // Parse the WS-API reply and hand it to the trader via the response sink. The
    // request id was set to the order's cid, so both success and error replies
    // correlate back by cid. Branch on status (partial read) before fully parsing
    // the result/error object.
    constexpr glz::opts classify_opts{
        .format = glz::JSON, .error_on_unknown_keys = false, .partial_read = true
    };
    constexpr glz::opts read_opts{.format = glz::JSON, .error_on_unknown_keys = false};

    WsApiResponseClassifier classifier;
    if (glz::read<classify_opts>(classifier, payload) || classifier.id.empty()) return;

    OG::OrderResponse response;
    try {
        response.cid = std::stoull(classifier.id);
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "WS-API reply with non-numeric id '{}': {}", classifier.id, e.what());
        return;
    }

    if (classifier.status == 200) {
        response.success = true;
        WsApiSuccessResponse ok;
        if (!glz::read<read_opts>(ok, payload) && ok.result.orderId != 0) {
            response.order_no = std::to_string(ok.result.orderId);
        }
    } else {
        response.success = false;
        WsApiErrorResponse err;
        if (!glz::read<read_opts>(err, payload)) {
            response.code = std::to_string(err.error.code);
            response.msg = err.error.msg;
            // -2011 "Unknown order sent." / -2013 "Order does not exist." Both mean
            // the venue has no such live order, so the trader should drop it rather
            // than keep chasing it with cancels.
            response.order_gone = (err.error.code == -2011 || err.error.code == -2013);
        }
    }

    deliver(response);
}


bool WsOrderGateway::send_request(
    const std::string& method, std::map<std::string, std::string> params, uint64_t cid
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

    WsApiRequest req{
        .id = std::to_string(cid),     // reply correlates back by cid
        .method = method,
        .params = std::move(params)    // all string values
    };
    std::string payload;
    if (glz::write_json(req, payload)) {
        LOG_WARNING(logger_, "WS-API request serialization failed");
        return false;
    }

    try {
        std::lock_guard<std::mutex> lock(client_mutex_);
        // Rechecked under the lock: connected_ may have been true a moment ago and
        // the client already swapped out by a redial.
        if (!client_ || !connected_.load()) return false;
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
    // Only sent when asked for: USD-M futures accepts reduceOnly in One-way mode and
    // rejects it in Hedge Mode, so the flag has to stay opt-in.
    if (info.reduce_only) params["reduceOnly"] = "true";
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
