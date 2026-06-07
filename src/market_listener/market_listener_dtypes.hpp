#pragma once
#include <string>
#include <vector>
#include <variant>
#include <cstdint>
#include <argparse/argparse.hpp>

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
    ListenerStatusUpdate, OrderbookMsg, TradeMsg, ExecutionMsg, PositionMsg,
    BalanceMsg, ProductInfoMsg
>;

struct ListenerConfig {
    std::string exchange = "binance";
    std::string region = "";
    std::string market_type = "derivatives";
    bool is_night = false;
    long timezone_minute_offset = 0;
    std::vector<std::string> products = {"BTCUSDT"};
    std::string products_db_base_path = "products";
    std::string orderbook_save_path = "orderbook_record";
    std::string trade_save_path = "trade_record";
    std::string broadcast_host_address = "0.0.0.0";
    unsigned short broadcast_port = 8888;
    std::string domain_type = "real";    // selects real vs test endpoints
    int orderbook_levels = 20;           // top-N levels to broadcast

    // Loop-control / logging (not consumed by the adapters but owned here so the
    // whole listener configuration is registered and populated in one place).
    long market_end_intraday_minute = -1;
    std::string log_path = "logs/listener.log";

    static void set_parser(argparse::ArgumentParser& program) {
        program.add_argument("--exchange").default_value(std::string("binance"));
        program.add_argument("--region").default_value(std::string(""));
        program.add_argument("--market_type").default_value(std::string("derivatives"));
        program.add_argument("--is_night").flag();
        program.add_argument("--timezone_minute_offset").scan<'i', int64_t>().default_value(int64_t{0});
        program.add_argument("--market_end_intraday_minute").scan<'i', int64_t>().default_value(int64_t{-1});
        program.add_argument("--products")
            .nargs(argparse::nargs_pattern::any)
            .default_value(std::vector<std::string>{"BTCUSDT"});
        program.add_argument("--products_db_base_path").default_value(std::string("products"));
        program.add_argument("--orderbook_save_path").default_value(std::string("orderbook_record"));
        program.add_argument("--trade_save_path").default_value(std::string("trade_record"));
        program.add_argument("--orderbook_levels").scan<'i', int>().default_value(20);
        program.add_argument("--broadcast_host_address").default_value(std::string("0.0.0.0"));
        program.add_argument("--broadcast_port").scan<'i', int>().default_value(8888);
        program.add_argument("--domain_type").default_value(std::string("real"));
        program.add_argument("--log_path").default_value(std::string("logs/listener.log"));
    }

    void init(const argparse::ArgumentParser& program) {
        exchange = program.get<std::string>("--exchange");
        region = program.get<std::string>("--region");
        market_type = program.get<std::string>("--market_type");
        is_night = program.get<bool>("--is_night");
        timezone_minute_offset = program.get<int64_t>("--timezone_minute_offset");
        market_end_intraday_minute = program.get<int64_t>("--market_end_intraday_minute");
        products = program.get<std::vector<std::string>>("--products");
        products_db_base_path = program.get<std::string>("--products_db_base_path");
        orderbook_save_path = program.get<std::string>("--orderbook_save_path");
        trade_save_path = program.get<std::string>("--trade_save_path");
        orderbook_levels = program.get<int>("--orderbook_levels");
        broadcast_host_address = program.get<std::string>("--broadcast_host_address");
        broadcast_port = static_cast<unsigned short>(program.get<int>("--broadcast_port"));
        domain_type = program.get<std::string>("--domain_type");
        log_path = program.get<std::string>("--log_path");
    }
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
