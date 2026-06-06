#pragma once
#include <string>
#include <vector>
#include <variant>

#include "common/market_msg_types.hpp"

namespace Omni::Listener {

// Connection lifecycle notification emitted by an adapter (one per socket).
struct ListenerStatusUpdate {
    std::string connection;   // e.g. "binance_market", "binance_user", "kis"
    bool connecting = false;
    bool opened = false;
};

// Normalized events flowing adapter -> MarketListener. The listener broadcasts
// these over TCP and (for market data) CSV-logs them.
using ListenerEvent = std::variant<
    ListenerStatusUpdate, OrderbookMsg, TradeMsg, ExecutionMsg, PositionMsg
>;

struct ListenerConfig {
    std::string exchange = "binance";
    std::string region = "";
    std::string market_type = "derivatives";
    bool is_night = false;
    long timezone_minute_offset = 0;
    std::vector<std::string> codes = {"BTCUSDT"};
    std::string codes_db_base_path = "codes";
    std::string orderbook_save_path = "orderbook_record";
    std::string trade_save_path = "trade_record";
    std::string broadcast_host_address = "0.0.0.0";
    unsigned short broadcast_port = 8888;
    std::string domain_type = "real";    // selects real vs test endpoints
    int orderbook_levels = 20;           // top-N levels to broadcast
};

struct OrderbookCsvSchema {
    static constexpr char const* header = "local_tstamp,is_bid,price,qty";
    static constexpr char const* format = "{},{},{},{}";
};

struct TradeCsvSchema {
    static constexpr char const* header = "local_tstamp,trade_price,cum_trade_qty,cum_buy_trade_qty";
    static constexpr char const* format = "{},{},{},{}";
};

} // namespace Omni::Listener
