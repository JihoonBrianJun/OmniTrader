#pragma once
#include <string>
#include <vector>
#include <set>
#include <map>
#include <variant>
#include <cmath>

#include "common/market_msg_types.hpp"
#include "trader/order_gateway/order_gateway_dtypes.hpp"


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

struct TraderConfig {
    std::string exchange = "binance";
    std::string region = "";
    std::string market_type = "derivatives";
    bool is_night = false;
    double min_tick_size = 0.01;
    double lot_size = 1.0;
    std::vector<std::string> trade_codes = {"BTCUSDT"};
    bool subscribe_same_codes = true;
    std::vector<std::string> subscribe_codes = {"BTCUSDT"};
    std::string broadcast_host_address = "0.0.0.0";
    unsigned short broadcast_port = 8888;
    std::string http_domain_type = "rest_real";
    std::string ws_api_domain_type = "ws_api_real";
    long order_update_interval_ms = 1000;
};

struct BBO {
    double bbid_price;
    double bask_price;
    double bbid_qty = 0;
    double bask_qty = 0;
    double mid_price = NAN;
    double vwap = NAN;
};

struct OutstandingOrderInfo {
    double price;
    long left_qty_in_lots;
};
using CodeOutstandingOrders = std::map<std::string, OutstandingOrderInfo>;

struct PendingOrders {
    std::set<std::string> pending_place_orders;
    std::set<std::string> pending_amend_orders;
    std::set<std::string> pending_cancel_orders;
};

} // namespace Omni::Trader
