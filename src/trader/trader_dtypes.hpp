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
        s.get("timezone_minute_offset", timezone_minute_offset);
        s.get("market_end_intraday_minute", market_end_intraday_minute);
        s.get("log_base_path", log_base_path);
        s.get("log_path", log_path);
        s.done();
    }
};

} // namespace Omni::Trader
