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
#include "pricer/fair_price/fair_price.hpp"

namespace Omni::Pricer {

// Timer tick: recompute and publish the fair price for one product.
struct FairPriceUpdate {
    std::string product;
};

// The pricer sits between the two other executables, so its task queue carries the
// listener's feed tasks plus its own publish timer.
using Task = std::variant<
    ListenerStatusUpdate, ListenerSubscribeUpdate, MarketDataResponse, FairPriceUpdate
>;

// Service wiring. The factor's own common config lives in FactorConfig
// (factor/base_factor.hpp), registered alongside this one, mirroring how the trader
// registers TraderConfig and MarketConfig side by side.
struct PricerConfig {
    // How fair price is derived. MID needs no factor and no market state beyond the
    // book, so a pricer with no arguments at all is a working mid publisher.
    FairPriceMode mode = FairPriceMode::MID;

    // Products to compute (and publish) a fair price for.
    std::vector<ProductSpec> products = {{"BTCUSDT", Category::futures}};

    // Upstream: the listener's broadcast server.
    std::string broadcast_host_address = "0.0.0.0";
    unsigned short broadcast_port = 8888;

    // Downstream: this service's own server, which traders subscribe to.
    std::string publish_host_address = "0.0.0.0";
    unsigned short publish_port = 8889;

    long publish_interval_ms = 100;

    // Loop-control / logging.
    long timezone_minute_offset = 0;
    long market_end_intraday_minute = -1;
    // Silences the per-publish [FairPrice] lines, which are one per product per tick
    // and dominate the file. Startup, subscription and warning lines are unaffected.
    bool no_pricer_log = false;
    std::string log_path = "logs/pricer/pricer.log";

    static void set_parser(argparse::ArgumentParser& program) {
        program.add_argument("--fair_price_mode")
            .default_value(std::string("MID"))
            .choices("MID", "VWAP", "FACTOR");
        program.add_argument("--products")
            .nargs(argparse::nargs_pattern::any)
            .default_value(std::vector<std::string>{"BTCUSDT:futures"});   // SYMBOL[:category]
        program.add_argument("--broadcast_host_address").default_value(std::string("0.0.0.0"));
        program.add_argument("--broadcast_port").scan<'i', int>().default_value(8888);
        program.add_argument("--publish_host_address").default_value(std::string("0.0.0.0"));
        program.add_argument("--publish_port").scan<'i', int>().default_value(8889);
        program.add_argument("--publish_interval_ms")
            .scan<'i', int64_t>().default_value(int64_t{100});
        program.add_argument("--timezone_minute_offset").scan<'i', int64_t>().default_value(int64_t{0});
        program.add_argument("--market_end_intraday_minute")
            .scan<'i', int64_t>().default_value(int64_t{-1});
        program.add_argument("--no-pricer-log").flag();
        program.add_argument("--log_path").default_value(std::string("logs/pricer/pricer.log"));
    }

    void init(const argparse::ArgumentParser& program) {
        mode = fair_price_mode_from_string(program.get<std::string>("--fair_price_mode"));
        products.clear();
        for (const auto& token : program.get<std::vector<std::string>>("--products")) {
            products.push_back(product_spec_from_token(token));
        }
        broadcast_host_address = program.get<std::string>("--broadcast_host_address");
        broadcast_port = static_cast<unsigned short>(program.get<int>("--broadcast_port"));
        publish_host_address = program.get<std::string>("--publish_host_address");
        publish_port = static_cast<unsigned short>(program.get<int>("--publish_port"));
        publish_interval_ms = program.get<int64_t>("--publish_interval_ms");
        timezone_minute_offset = program.get<int64_t>("--timezone_minute_offset");
        market_end_intraday_minute = program.get<int64_t>("--market_end_intraday_minute");
        no_pricer_log = program.get<bool>("--no-pricer-log");
        log_path = program.get<std::string>("--log_path");
    }

    // File-based alternative to init(): reads the same fields from the
    // "pricer_config" section of a launch-config document (config/<exchange>/
    // pricer0.json). Absent fields keep the defaults above.
    void parse_json(const nlohmann::json& doc) {
        Omni::Config::JsonSection s(doc, "pricer_config");
        if (s.has("fair_price_mode")) {
            std::string mode_name;
            s.get("fair_price_mode", mode_name);
            mode = fair_price_mode_from_string(mode_name);
        }
        s.skip("fair_price_mode");
        // Same SYMBOL[:category] tokens the CLI takes, as a JSON array.
        if (s.has("products")) {
            std::vector<std::string> tokens;
            s.get("products", tokens);
            products.clear();
            for (const auto& token : tokens) {
                products.push_back(product_spec_from_token(token));
            }
        }
        s.skip("products");
        s.get("broadcast_host_address", broadcast_host_address);
        s.get("broadcast_port", broadcast_port);
        s.get("publish_host_address", publish_host_address);
        s.get("publish_port", publish_port);
        s.get("publish_interval_ms", publish_interval_ms);
        s.get("timezone_minute_offset", timezone_minute_offset);
        s.get("market_end_intraday_minute", market_end_intraday_minute);
        s.get("no_pricer_log", no_pricer_log);
        s.get("log_path", log_path);
        s.done();
    }
};

} // namespace Omni::Pricer
