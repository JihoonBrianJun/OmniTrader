#pragma once
#include <string>
#include <vector>
#include <variant>
#include <cstdint>
#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>

#include "common/market_msg_types.hpp"
#include "common/feed_msg_types.hpp"
#include "config_handlers/json_config.hpp"
#include "trader/order_gateway/order_gateway_dtypes.hpp"

namespace Omni::Trader {

// The feed task types live in common/feed_msg_types.hpp (shared with the pricer);
// unqualified uses below resolve to the enclosing Omni namespace.

struct OrderUpdate {
    std::string product;
};

// Async order-gateway reply (place/amend/cancel outcome), delivered from the
// gateway's response sink and correlated back to the order by cid.
using OrderResponseTask = Omni::OrderGateway::OrderResponse;

// One queue, fed by three producers: the listener link's io thread, the pricer link's
// io thread, and the order gateway's response sink. Which of them enqueued a task is
// answered by its type, so run() dispatches with no branching.
//
// Tasks from different producers are therefore not ordered against each other -- a
// fair price and an orderbook update that arrive together may be handled in either
// order. That is harmless here: the fair price carries the timestamp the pricer
// computed it at and ages out on its own, independently of the book.
using Task = std::variant<
    ListenerStatusUpdate, ListenerSubscribeUpdate, MarketDataResponse,
    PricerStatusUpdate, PricerSubscribeUpdate, FairPriceResponse,
    OrderUpdate, OrderResponseTask
>;

// Wiring/config consumed by the order handler and the order-gateway factories.
struct TraderConfig {
    std::string exchange = "binance";
    std::string strategy_name = "Geuant";
    std::string region = "";
    std::string market_type = "derivatives";
    std::vector<std::string> trade_products = {"BTCUSDT"};   // bare symbols (tradable, futures)
    bool subscribe_same_products = true;
    std::vector<ProductSpec> subscribe_products = {{"BTCUSDT", Category::futures}};
    std::string broadcast_host_address = "0.0.0.0";
    unsigned short broadcast_port = 8888;
    // Second internal link, to the pricer executable, which is where fair price comes
    // from. Always dialled; if nothing is listening the trader simply keeps pricing
    // off its own mid.
    std::string pricer_host_address = "0.0.0.0";
    unsigned short pricer_port = 8889;
    // How long a received fair price stays usable. A pricer that died or a link that
    // dropped must not go on skewing quotes, so the value ages out and the trader
    // falls back to mid. 0 disables ageing.
    long fair_price_max_age_ms = 1000;
    std::string domain_type = "real";   // selects real vs test endpoints (REST + WS-API)
    long order_update_interval_ms = 1000;

    // --- Shutdown ---------------------------------------------------------------
    // However the session ends -- Ctrl-C, a supervisor's SIGTERM, the market close --
    // the trader first pulls every resting order and then tries to get flat, so it
    // does not leave quotes working or a position on with nothing watching it.
    //
    // Cancelling is unconditional: an order the trader can no longer manage has no
    // business staying live. Flattening is a position decision, so it is a switch.
    bool flatten_on_shutdown = true;
    // How long to keep chasing cancel acks before giving up and logging what is left.
    // Sized for the slow path: a gateway sends its requests one at a time to keep them
    // in order, so cancelling N resting orders over REST costs N round trips, not one.
    long shutdown_cancel_timeout_ms = 5000;
    // How long to work the position passively (the strategy's liquidation quote, one
    // tick inside the touch) before falling back to a market order.
    long shutdown_flatten_timeout_ms = 5000;
    // Send that market order for whatever the passive stage could not get done. Off
    // means the trader exits with the residual and says so loudly, which is the right
    // choice on a venue or account where an automatic market order is unwelcome.
    bool shutdown_market_flatten = true;
    // Tag the flattening orders reduceOnly, so a stale position count can never turn a
    // close into a new position the other way. Turn this off for a Binance futures
    // account in Hedge Mode, which rejects the flag outright.
    bool shutdown_reduce_only = true;

    // --- CSV records ------------------------------------------------------------
    // The trader's own csv trails, written in the same layout the listener uses for
    // its orderbook/trade records: <save_path>/<exchange>/<product>/<date>.log.
    // An empty path turns that record off.
    std::string order_record_save_path = "order_record";
    std::string order_decision_save_path = "order_decision";

    // Loop-control / logging.
    long timezone_minute_offset = 0;
    long market_end_intraday_minute = -1;
    std::string log_base_path = "./log";
    std::string log_path = "logs/trader.log";

    // Minimal first-pass parser to learn exchange/strategy before the
    // strategy-specific args are registered (two-phase parse).
    static void set_header_parser(argparse::ArgumentParser& program) {
        program.add_argument("--exchange").default_value(std::string("binance"));
        program.add_argument("--strategy_name").default_value(std::string("Geuant"));
    }

    static void set_parser(argparse::ArgumentParser& program) {
        set_header_parser(program);
        program.add_argument("--region").default_value(std::string(""));
        program.add_argument("--market_type").default_value(std::string("derivatives"));
        program.add_argument("--trade_products")
            .nargs(argparse::nargs_pattern::any)
            .default_value(std::vector<std::string>{"BTCUSDT:futures"});   // SYMBOL[:category]
        program.add_argument("--subscribe_products")
            .nargs(argparse::nargs_pattern::any)
            .default_value(std::vector<std::string>{});
        program.add_argument("--broadcast_host_address").default_value(std::string("0.0.0.0"));
        program.add_argument("--broadcast_port").scan<'i', int>().default_value(8888);
        program.add_argument("--pricer_host_address").default_value(std::string("0.0.0.0"));
        program.add_argument("--pricer_port").scan<'i', int>().default_value(8889);
        program.add_argument("--fair_price_max_age_ms")
            .scan<'i', int64_t>().default_value(int64_t{1000});
        program.add_argument("--domain_type").default_value(std::string("real"));
        program.add_argument("--order_update_interval_ms").scan<'i', int64_t>().default_value(int64_t{1000});
        // Shutdown behaviour. The two defaults-on switches are expressed as
        // --no_... flags so the safe behaviour needs no argument to get.
        program.add_argument("--no_flatten_on_shutdown").flag();
        program.add_argument("--shutdown_cancel_timeout_ms")
            .scan<'i', int64_t>().default_value(int64_t{5000});
        program.add_argument("--shutdown_flatten_timeout_ms")
            .scan<'i', int64_t>().default_value(int64_t{5000});
        program.add_argument("--no_shutdown_market_flatten").flag();
        program.add_argument("--no_shutdown_reduce_only").flag();
        program.add_argument("--order_record_save_path").default_value(std::string("order_record"));
        program.add_argument("--order_decision_save_path").default_value(std::string("order_decision"));
        program.add_argument("--timezone_minute_offset").scan<'i', int64_t>().default_value(int64_t{0});
        program.add_argument("--market_end_intraday_minute").scan<'i', int64_t>().default_value(int64_t{-1});
        program.add_argument("--log_base_path").default_value(std::string("./log"));
        program.add_argument("--log_path").default_value(std::string("logs/trader.log"));
    }

    void init(const argparse::ArgumentParser& program) {
        exchange = program.get<std::string>("--exchange");
        strategy_name = program.get<std::string>("--strategy_name");
        region = program.get<std::string>("--region");
        market_type = program.get<std::string>("--market_type");
        // Tokens are SYMBOL[:category] (category defaults to futures). trade_products
        // keeps bare symbols; subscribe_products keeps (symbol, category) specs and
        // falls back to the trade products (with their categories) when not given.
        std::vector<ProductSpec> trade_specs;
        trade_products.clear();
        for (const auto& token : program.get<std::vector<std::string>>("--trade_products")) {
            auto spec = product_spec_from_token(token);
            trade_products.push_back(spec.product);
            trade_specs.push_back(spec);
        }
        auto subscribe_tokens = program.get<std::vector<std::string>>("--subscribe_products");
        subscribe_same_products = subscribe_tokens.empty();
        subscribe_products.clear();
        if (subscribe_same_products) {
            subscribe_products = trade_specs;
        } else {
            for (const auto& token : subscribe_tokens) {
                subscribe_products.push_back(product_spec_from_token(token));
            }
        }
        broadcast_host_address = program.get<std::string>("--broadcast_host_address");
        broadcast_port = static_cast<unsigned short>(program.get<int>("--broadcast_port"));
        pricer_host_address = program.get<std::string>("--pricer_host_address");
        pricer_port = static_cast<unsigned short>(program.get<int>("--pricer_port"));
        fair_price_max_age_ms = program.get<int64_t>("--fair_price_max_age_ms");
        domain_type = program.get<std::string>("--domain_type");
        order_update_interval_ms = program.get<int64_t>("--order_update_interval_ms");
        flatten_on_shutdown = !program.get<bool>("--no_flatten_on_shutdown");
        shutdown_cancel_timeout_ms = program.get<int64_t>("--shutdown_cancel_timeout_ms");
        shutdown_flatten_timeout_ms = program.get<int64_t>("--shutdown_flatten_timeout_ms");
        shutdown_market_flatten = !program.get<bool>("--no_shutdown_market_flatten");
        shutdown_reduce_only = !program.get<bool>("--no_shutdown_reduce_only");
        order_record_save_path = program.get<std::string>("--order_record_save_path");
        order_decision_save_path = program.get<std::string>("--order_decision_save_path");
        timezone_minute_offset = program.get<int64_t>("--timezone_minute_offset");
        market_end_intraday_minute = program.get<int64_t>("--market_end_intraday_minute");
        log_base_path = program.get<std::string>("--log_base_path");
        log_path = program.get<std::string>("--log_path");
    }

    // File-based alternative to init(): reads the same fields from the
    // "trader_config" section of a launch-config document (config/<exchange>/
    // trader0.json). Absent fields keep the defaults above.
    void parse_json(const nlohmann::json& doc) {
        Omni::Config::JsonSection s(doc, "trader_config");
        s.get("exchange", exchange);
        s.get("strategy_name", strategy_name);
        s.get("region", region);
        s.get("market_type", market_type);
        // Tokens are SYMBOL[:category], as on the CLI. trade_products keeps bare
        // symbols; subscribe_products keeps specs and falls back to the trade
        // products when the key is absent or empty.
        std::vector<ProductSpec> trade_specs;
        if (s.has("trade_products")) {
            std::vector<std::string> tokens;
            s.get("trade_products", tokens);
            trade_products.clear();
            for (const auto& token : tokens) {
                auto spec = product_spec_from_token(token);
                trade_products.push_back(spec.product);
                trade_specs.push_back(spec);
            }
        } else {
            for (const auto& product : trade_products) {
                trade_specs.push_back(product_spec_from_token(product));
            }
        }
        s.skip("trade_products");

        std::vector<std::string> subscribe_tokens;
        s.get("subscribe_products", subscribe_tokens);
        subscribe_same_products = subscribe_tokens.empty();
        subscribe_products.clear();
        if (subscribe_same_products) {
            subscribe_products = trade_specs;
        } else {
            for (const auto& token : subscribe_tokens) {
                subscribe_products.push_back(product_spec_from_token(token));
            }
        }

        s.get("broadcast_host_address", broadcast_host_address);
        s.get("broadcast_port", broadcast_port);
        s.get("pricer_host_address", pricer_host_address);
        s.get("pricer_port", pricer_port);
        s.get("fair_price_max_age_ms", fair_price_max_age_ms);
        s.get("domain_type", domain_type);
        s.get("order_update_interval_ms", order_update_interval_ms);
        // Stated positively here. The CLI spells the two defaults-on switches as
        // --no_flatten_on_shutdown / --no_shutdown_market_flatten / --no_shutdown_
        // reduce_only, since a flag can only turn something on; a JSON field can just
        // say false.
        s.get("flatten_on_shutdown", flatten_on_shutdown);
        s.get("shutdown_cancel_timeout_ms", shutdown_cancel_timeout_ms);
        s.get("shutdown_flatten_timeout_ms", shutdown_flatten_timeout_ms);
        s.get("shutdown_market_flatten", shutdown_market_flatten);
        s.get("shutdown_reduce_only", shutdown_reduce_only);
        s.get("order_record_save_path", order_record_save_path);
        s.get("order_decision_save_path", order_decision_save_path);
        s.get("timezone_minute_offset", timezone_minute_offset);
        s.get("market_end_intraday_minute", market_end_intraday_minute);
        s.get("log_base_path", log_base_path);
        s.get("log_path", log_path);
        s.done();
    }
};

// One row per order action the trader took, written when the gateway's reply lands
// so `success` and `order_no` are on the same row as the request that produced them.
// `type` is place/amend/cancel and `side` is bid/ask. For a place, price/qty are the
// values actually sent to the venue -- after the snap onto the product's grid, and
// with a NaN price for a market order, which carries none. For a cancel they are the
// resting order being pulled, as the trader had it recorded.
//
// `symbol` is carried even though the file is already per product, so records for
// several products can be concatenated without losing which is which.
// `server_tstamp` is the venue's own epoch-ms stamp for the reply, 0 when it sent
// none (a rejection, or a request that never left the process). Kept next to
// local_tstamp rather than replacing it: the gap between the two is the only
// measure of round-trip and clock skew this file can offer.
//
// `msg` is the venue's error text, "-" when the request succeeded. Commas and
// newlines are stripped on the way in, since neither survives a CSV field.
struct OrderRecordCsvSchema {
    static constexpr char const* header =
        "local_tstamp,server_tstamp,symbol,cid,order_no,type,side,success,price,qty,msg";
    static constexpr char const* format = "{},{},{},{},{},{},{},{},{},{},{}";
};

// One row per decision that reached the strategy: everything it was given, and the
// best quote it asked for on each side (NaN when it wanted none there). Ticks that
// never got as far as the strategy -- no book, stale link, awaiting replies -- write
// no row; the trader log says why.
struct OrderDecisionCsvSchema {
    static constexpr char const* header =
        "local_tstamp,bbid,bask,mid,fair,factor,position_lots,outstanding,bid_price,ask_price";
    static constexpr char const* format = "{},{},{},{},{},{},{},{},{},{}";
};

} // namespace Omni::Trader
