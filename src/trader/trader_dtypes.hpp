#pragma once
#include <string>
#include <vector>
#include <variant>
#include <cstdint>
#include <argparse/argparse.hpp>

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
    std::string product;
};

struct TcpMarketDataResponse {
    enum Feed {
        Orderbook,
        Trade,
        Execution,
        Position,
        ProductInfo,
        Error
    } feed;
    std::string product;   // for an asset position this carries the asset (e.g. "USDC")
    std::variant<OrderbookData, TradeData, ExecutionData, PositionData, ProductInfoData> data;
};

struct OrderUpdate {
    std::string product;
};

// Async order-gateway reply (place/amend/cancel outcome), delivered from the
// gateway's response sink and correlated back to the order by cid.
using OrderResponseTask = Omni::OrderGateway::OrderResponse;

using Task = std::variant<
    TcpStatusUpdate, TcpSubscribeUpdate, TcpMarketDataResponse, OrderUpdate, OrderResponseTask
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
            .default_value(std::vector<std::string>{"BTCUSDT"});
        program.add_argument("--subscribe_products")
            .nargs(argparse::nargs_pattern::any)
            .default_value(std::vector<std::string>{});
        program.add_argument("--broadcast_host_address").default_value(std::string("0.0.0.0"));
        program.add_argument("--broadcast_port").scan<'i', int>().default_value(8888);
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
        domain_type = program.get<std::string>("--domain_type");
        order_update_interval_ms = program.get<int64_t>("--order_update_interval_ms");
        timezone_minute_offset = program.get<int64_t>("--timezone_minute_offset");
        market_end_intraday_minute = program.get<int64_t>("--market_end_intraday_minute");
        log_base_path = program.get<std::string>("--log_base_path");
        log_path = program.get<std::string>("--log_path");
    }
};

} // namespace Omni::Trader
