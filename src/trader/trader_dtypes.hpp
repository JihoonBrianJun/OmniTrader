#pragma once
#include <string>
#include <vector>
#include <variant>

#include "common/market_msg_types.hpp"

namespace Omni::Trader {

struct TcpStatusUpdate {
    bool connecting;
    bool connected;
};

struct TcpSubscribeUpdate {
    bool subscribe;
    bool success;
    std::string code;
};

struct TcpMarketDataResponse {
    enum Feed {
        Orderbook,
        Trade,
        Execution,
        Position,
        Error
    } feed;
    std::string code;
    std::variant<OrderbookData, TradeData, ExecutionData, PositionData> data;
};

struct OrderUpdate {
    std::string code;
};

using Task = std::variant<
    TcpStatusUpdate, TcpSubscribeUpdate, TcpMarketDataResponse, OrderUpdate
>;

// Wiring/config consumed by the order handler and the order-gateway factories.
struct TraderConfig {
    std::string exchange = "binance";
    std::string region = "";
    std::string market_type = "derivatives";
    std::vector<std::string> trade_codes = {"BTCUSDT"};
    bool subscribe_same_codes = true;
    std::vector<std::string> subscribe_codes = {"BTCUSDT"};
    std::string broadcast_host_address = "0.0.0.0";
    unsigned short broadcast_port = 8888;
    std::string http_domain_type = "rest_real";
    long order_update_interval_ms = 1000;
};

} // namespace Omni::Trader
