#pragma once
#include <string>
#include <vector>
#include <variant>
#include <cstdint>
#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>

#include "common/market_msg_types.hpp"
#include "config_handlers/json_config.hpp"

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
    ListenerStatusUpdate, OrderbookMsg, TradeMsg, ExecutionMsg, PositionMsg,
    ProductInfoMsg
>;

struct ListenerConfig {
    // Identifies this instance: the launch config's file name with the service prefix
    // stripped (listener0.json -> "0"). Becomes a directory level under log_path and
    // the record save paths, so two listeners never write to the same file.
    std::string name = "";

    std::string exchange = "binance";
    std::string region = "";
    std::string market_type = "derivatives";
    bool is_night = false;
    long timezone_minute_offset = 0;
    std::vector<std::string> products = {"BTCUSDT"};   // bare symbols (all categories)
    std::vector<ProductSpec> product_specs = {{"BTCUSDT", Category::futures}};
    std::string products_db_base_path = "products";
    std::string orderbook_save_path = "logs/orderbook_record";
    std::string trade_save_path = "logs/trade_record";
    bool no_orderbook_log = false;
    bool no_public_trade_log = false;
    std::string broadcast_host_address = "0.0.0.0";
    unsigned short broadcast_port = 8888;
    std::string domain_type = "real";    // selects real vs test endpoints
    int orderbook_levels = 20;           // top-N levels to broadcast
    long product_info_refresh_sec = 300; // re-fetch/publish product info every N s (<=0 disables)

    // Loop-control / logging (not consumed by the adapters but owned here so the
    // whole listener configuration is registered and populated in one place).
    long market_end_intraday_minute = -1;
    std::string log_path = "logs/listener/listener.log";

    static void set_parser(argparse::ArgumentParser& program) {
        program.add_argument("--exchange").default_value(std::string("binance"));
        program.add_argument("--region").default_value(std::string(""));
        program.add_argument("--market_type").default_value(std::string("derivatives"));
        program.add_argument("--is_night").flag();
        program.add_argument("--timezone_minute_offset").scan<'i', int64_t>().default_value(int64_t{0});
        program.add_argument("--market_end_intraday_minute").scan<'i', int64_t>().default_value(int64_t{-1});
        program.add_argument("--products")
            .nargs(argparse::nargs_pattern::any)
            .default_value(std::vector<std::string>{"BTCUSDT:futures"});   // SYMBOL[:category]
        program.add_argument("--products_db_base_path").default_value(std::string("products"));
        program.add_argument("--orderbook_save_path").default_value(std::string("logs/orderbook_record"));
        program.add_argument("--trade_save_path").default_value(std::string("logs/trade_record"));
        program.add_argument("--name").default_value(std::string(""));
        program.add_argument("--no-orderbook-log").flag();
        program.add_argument("--no-public-trade-log").flag();
        program.add_argument("--orderbook_levels").scan<'i', int>().default_value(20);
        program.add_argument("--product_info_refresh_sec").scan<'i', int64_t>().default_value(int64_t{300});
        program.add_argument("--broadcast_host_address").default_value(std::string("0.0.0.0"));
        program.add_argument("--broadcast_port").scan<'i', int>().default_value(8888);
        program.add_argument("--domain_type").default_value(std::string("real"));
        program.add_argument("--log_path").default_value(std::string("logs/listener/listener.log"));
    }

    void init(const argparse::ArgumentParser& program) {
        exchange = program.get<std::string>("--exchange");
        region = program.get<std::string>("--region");
        market_type = program.get<std::string>("--market_type");
        is_night = program.get<bool>("--is_night");
        timezone_minute_offset = program.get<int64_t>("--timezone_minute_offset");
        market_end_intraday_minute = program.get<int64_t>("--market_end_intraday_minute");
        // Each --products token is SYMBOL[:category] (category defaults to futures).
        products.clear();
        product_specs.clear();
        for (const auto& token : program.get<std::vector<std::string>>("--products")) {
            auto spec = product_spec_from_token(token);
            products.push_back(spec.product);
            product_specs.push_back(spec);
        }
        products_db_base_path = program.get<std::string>("--products_db_base_path");
        name = program.get<std::string>("--name");
        orderbook_save_path = Omni::Config::named_save_path(
            program.get<std::string>("--orderbook_save_path"), name
        );
        trade_save_path = Omni::Config::named_save_path(
            program.get<std::string>("--trade_save_path"), name
        );
        no_orderbook_log = program.get<bool>("--no-orderbook-log");
        no_public_trade_log = program.get<bool>("--no-public-trade-log");
        orderbook_levels = program.get<int>("--orderbook_levels");
        product_info_refresh_sec = program.get<int64_t>("--product_info_refresh_sec");
        broadcast_host_address = program.get<std::string>("--broadcast_host_address");
        broadcast_port = static_cast<unsigned short>(program.get<int>("--broadcast_port"));
        domain_type = program.get<std::string>("--domain_type");
        log_path = Omni::Config::named_log_path(program.get<std::string>("--log_path"), name);
    }

    // File-based alternative to init(): reads the same fields from the
    // "listener_config" section of a launch-config document (config/<exchange>/
    // listener0.json). Absent fields keep the defaults above.
    void parse_json(const nlohmann::json& doc) {
        Omni::Config::JsonSection s(doc, "listener_config");
        s.get("exchange", exchange);
        s.get("region", region);
        s.get("market_type", market_type);
        s.get("is_night", is_night);
        s.get("timezone_minute_offset", timezone_minute_offset);
        s.get("market_end_intraday_minute", market_end_intraday_minute);
        // Same SYMBOL[:category] tokens the CLI takes, as a JSON array.
        if (s.has("products")) {
            std::vector<std::string> tokens;
            s.get("products", tokens);
            products.clear();
            product_specs.clear();
            for (const auto& token : tokens) {
                auto spec = product_spec_from_token(token);
                products.push_back(spec.product);
                product_specs.push_back(spec);
            }
        }
        s.skip("products");
        s.get("name", name);
        s.get("products_db_base_path", products_db_base_path);
        s.get("orderbook_save_path", orderbook_save_path);
        s.get("trade_save_path", trade_save_path);
        s.get("no_orderbook_log", no_orderbook_log);
        s.get("no_public_trade_log", no_public_trade_log);
        s.get("orderbook_levels", orderbook_levels);
        s.get("product_info_refresh_sec", product_info_refresh_sec);
        s.get("broadcast_host_address", broadcast_host_address);
        s.get("broadcast_port", broadcast_port);
        s.get("domain_type", domain_type);
        s.get("log_path", log_path);
        s.done();
        log_path = Omni::Config::named_log_path(log_path, name);
        orderbook_save_path = Omni::Config::named_save_path(orderbook_save_path, name);
        trade_save_path = Omni::Config::named_save_path(trade_save_path, name);
    }
};

// `server_tstamp` is the venue's epoch-ms stamp for the update, nan where the
// exchange sends none (KIS) or the row came from a REST resync rather than the
// stream. The gap against local_tstamp is the feed latency for that update.
struct OrderbookCsvSchema {
    static constexpr char const* header = "local_tstamp,server_tstamp,is_bid,price,qty";
    static constexpr char const* format = "{},{},{},{},{}";
};

struct TradeCsvSchema {
    static constexpr char const* header =
        "local_tstamp,server_tstamp,trade_price,cum_trade_qty,cum_buy_trade_qty";
    static constexpr char const* format = "{},{},{},{},{}";
};

} // namespace Omni::Listener
