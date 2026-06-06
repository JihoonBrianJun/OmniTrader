#pragma once
#include <string>
#include <optional>
#include <cmath>
#include <vector>

// Normalized, exchange-agnostic message model shared across the listener, the
// internal TCP link, and the trader. Each exchange adapter translates its native
// wire format into these structs; the trader only ever sees these.

namespace Omni {

struct SubscribeRequestMsg {
    std::string feed = "subscribe";
    bool subscribe = false;
    std::string product = "";
};

struct SubscribeResponseMsg {
    std::string feed = "subscribe";
    bool subscribe = false;
    bool success = false;
    std::string product = "";
};

struct OrderbookLevel {
    std::optional<double> price = std::nullopt;
    std::optional<double> qty = std::nullopt;
};
struct OrderbookData {
    std::vector<OrderbookLevel> bid_book = {};
    std::vector<OrderbookLevel> ask_book = {};
};
struct OrderbookMsg {
    std::string feed = "orderbook";
    std::string product = "";
    OrderbookData orderbook_data = {};
};

struct TradeData {
    std::optional<double> trade_price = std::nullopt;
    std::optional<double> cum_trade_qty = std::nullopt;
    std::optional<double> cum_buy_trade_qty = std::nullopt;
};
struct TradeMsg {
    std::string feed = "trade";
    std::string product = "";
    TradeData trade_data = {};
};

// Order/execution updates. KIS fills these from its market socket; Binance fills
// them from ORDER_TRADE_UPDATE on the user-data stream and from the openOrders
// REST snapshot. The booleans describe the event so the trader's outstanding /
// pending bookkeeping in trader.cpp stays exchange-agnostic.
struct ExecutionData {
    std::string account_no = "";
    std::string order_no = "";
    std::string original_order_no = "";
    std::string short_product = "";
    std::string full_product = "";
    bool is_accept_data = false;   // an accept/ack/state-change (not a fill)
    bool is_place = false;         // a new order acceptance
    bool is_cancel = false;        // a cancel/expire acceptance
    bool is_bid = false;
    bool is_accepted = false;
    bool is_rejected = false;
    bool is_executed = false;      // a fill
    // KIS derives position from fills; Binance has authoritative ACCOUNT_UPDATE
    // position snapshots, so it sets this false to avoid double-counting.
    bool update_position_on_fill = true;
    std::optional<double> order_price = std::nullopt;
    std::optional<double> order_qty = std::nullopt;
    std::optional<double> execute_price = std::nullopt;
    std::optional<double> execute_qty = std::nullopt;
};
struct ExecutionMsg {
    std::string feed = "execution";
    std::string product = "";
    ExecutionData execution_data = {};
};

// Authoritative absolute position snapshot/update (Binance ACCOUNT_UPDATE +
// positionRisk REST snapshot). position_amt is signed (long > 0, short < 0).
struct PositionData {
    std::optional<double> position_amt = std::nullopt;
    std::optional<double> entry_price = std::nullopt;
    std::optional<double> unrealized_pnl = std::nullopt;
};
struct PositionMsg {
    std::string feed = "position";
    std::string product = "";
    PositionData position_data = {};
};

// Per-product trading parameters published by the listener (Binance: from
// exchangeInfo filters; other exchanges may omit). The trader uses these instead
// of CLI-provided tick/lot.
struct ProductInfoData {
    std::optional<double> min_tick_size = std::nullopt;
    std::optional<double> lot_size = std::nullopt;
};
struct ProductInfoMsg {
    std::string feed = "product_info";
    std::string product = "";
    ProductInfoData product_info_data = {};
};

// Used for glaze partial read to classify an incoming TCP line by feed.
struct FeedClassifier {
    std::string feed;
};

} // namespace Omni
