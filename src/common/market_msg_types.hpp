#pragma once
#include <string>
#include <string_view>
#include <optional>
#include <cmath>
#include <vector>
#include <glaze/glaze.hpp>

// Normalized, exchange-agnostic message model shared across the listener, the
// internal TCP link, and the trader. Each exchange adapter translates its native
// wire format into these structs; the trader only ever sees these.

namespace Omni {

// What kind of product a symbol refers to. Drives which exchange endpoint the
// adapter uses (e.g. Binance: futures->positionRisk, asset->/fapi/v2/balance) and
// is carried in the subscribe handshake. Each exchange uses the subset it supports.
enum class Category { futures, spot, asset };

// Parse a CLI/config token into a Category (defaults to futures for unknown input).
inline Category category_from_string(std::string_view s) {
    if (s == "spot") return Category::spot;
    if (s == "asset") return Category::asset;
    return Category::futures;
}

// A configured product together with its category. Shared by the listener (which
// endpoints/streams to use) and the trader (what to put in its subscribe message).
struct ProductSpec {
    std::string product = "";
    Category category = Category::futures;
};

// Parse a CLI/config token of form SYMBOL[:category] into a ProductSpec.
inline ProductSpec product_spec_from_token(std::string_view token) {
    auto pos = token.find(':');
    if (pos == std::string_view::npos) return {std::string(token), Category::futures};
    return {std::string(token.substr(0, pos)), category_from_string(token.substr(pos + 1))};
}

struct SubscribeRequestMsg {
    std::string feed = "subscribe";
    bool subscribe = false;
    std::string product = "";
    Category category = Category::futures;
};

struct SubscribeResponseMsg {
    std::string feed = "subscribe";
    bool subscribe = false;
    bool success = false;
    std::string product = "";
    Category category = Category::futures;
};

struct OrderbookLevel {
    std::optional<double> price = std::nullopt;
    std::optional<double> qty = std::nullopt;
};
// `server_tstamp_ms` is the venue's own timestamp for the update, in epoch
// milliseconds. Left empty -- and recorded as nan -- by any exchange that does not
// send one (KIS among them), and by an orderbook rebuilt from a REST snapshot,
// which carries no stream event time. Empty means "the venue told us nothing",
// never "zero", which is why it is an optional rather than a sentinel.
struct OrderbookData {
    std::vector<OrderbookLevel> bid_book = {};
    std::vector<OrderbookLevel> ask_book = {};
    std::optional<int64_t> server_tstamp_ms = std::nullopt;
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
    std::optional<int64_t> server_tstamp_ms = std::nullopt;   // see OrderbookData
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
    std::string client_order_id = "";
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

// Unified position/balance snapshot/update, keyed by product. The meaning of
// `balance` depends on the subscription's category: for futures it is the signed
// position amount (long > 0, short < 0; Binance ACCOUNT_UPDATE + positionRisk); for
// asset it is the wallet balance (Binance /fapi/v2/balance + ACCOUNT_UPDATE "B").
// `available_balance` is what's free for new orders (asset snapshot only — the
// stream update carries balance but not available).
struct PositionData {
    std::optional<double> balance = std::nullopt;
    std::optional<double> available_balance = std::nullopt;
};
struct PositionMsg {
    std::string feed = "position";
    std::string product = "";
    PositionData position_data = {};
};

// A product's real trading grid on the venue, published by the listener: it is the
// process that talks to the exchange, so it is the one that can know it. Binance
// fills these in from exchangeInfo filters and refreshes them on an interval, since
// a venue can change a filter under a running session; exchanges without a
// per-product grid endpoint (KIS) publish nothing and the field stays absent.
//
// Named `tick_size`, not `min_tick_size`, because it is a different thing from the
// trader's process-global --min_tick_size: that one is the unit the trader
// normalizes every price into, this one is the increment the venue will actually
// accept. The trader applies these only when submitting an order.
struct ProductInfoData {
    std::optional<double> tick_size = std::nullopt;
    std::optional<double> lot_size = std::nullopt;
};
struct ProductInfoMsg {
    std::string feed = "product_info";
    std::string product = "";
    ProductInfoData product_info_data = {};
};

// A product's fair price, published by the pricer executable and consumed by the
// trader's strategy. This is the pricer's only output: the trader takes top-of-book
// straight from the listener and gets fair price from here, falling back to its own
// mid whenever this is missing or stale.
//
// `ts` is the ns timestamp at which the fair price was computed; the trader uses it
// to age the value out. `factor` is set only when the pricer runs in FACTOR mode
// (fair = factor * mid, matching orderbook-backtest's pre-computed `FactorTick`) and
// is carried purely so the trader can log which factor produced the price it used.
// `forward_vol` also comes from FACTOR mode alone, but unlike `factor` it is acted
// on: it widens or tightens the strategy's quoted spread.
struct FairPriceData {
    int64_t ts = 0;
    std::optional<double> fair_price = std::nullopt;
    std::optional<double> factor = std::nullopt;
    double forward_vol = 1.0;
};
struct FairPriceMsg {
    std::string feed = "fair_price";
    std::string product = "";
    FairPriceData fair_price_data = {};
};

// Used for glaze partial read to classify an incoming TCP line by feed.
struct FeedClassifier {
    std::string feed;
};

} // namespace Omni

// Serialize Category by name (e.g. "futures") over the wire rather than as an int.
template <>
struct glz::meta<Omni::Category> {
    using enum Omni::Category;
    static constexpr auto value = glz::enumerate(futures, spot, asset);
};
