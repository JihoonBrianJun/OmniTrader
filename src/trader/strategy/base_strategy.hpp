#pragma once

#include <map>
#include <vector>
#include <cmath>
#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <argparse/argparse.hpp>

#include "trader/pricer/pricer_dtypes.hpp"
#include "utils/tick_configs.hpp"

// Mirrors orderbook-backtest base_strategy (MarketConfig / OutstandingOrder /
// BaseStrategy), adapted to the Omni::Trader namespace and live trading.

namespace Omni::Trader {

struct MarketConfig {
    double min_tick_size;
    double lot_size;
    double contract_multiplier;
    double fee_rate_bp;
    std::string exchange;
    std::string product;
    std::string json_write_path;
    Omni::TickFunc tick_func;
    bool short_sell_unable = false;

    static void set_parser(argparse::ArgumentParser& program) {
        program.add_argument("--min_tick_size")
            .scan<'g', double>()
            .default_value(0.0);
        program.add_argument("--lot_size")
            .scan<'g', double>()
            .default_value(0.0);
        program.add_argument("--contract_multiplier")
            .scan<'g', double>()
            .default_value(1.0);
        program.add_argument("--fee_rate_bp")
            .scan<'g', double>()
            .default_value(0.0);
        program.add_argument("--short_sell_unable")
            .flag();
    }

    void init(const argparse::ArgumentParser& program) {
        min_tick_size = program.get<double>("--min_tick_size");
        lot_size = program.get<double>("--lot_size");
        contract_multiplier = program.get<double>("--contract_multiplier");
        fee_rate_bp = program.get<double>("--fee_rate_bp");
        exchange = program.get<std::string>("--exchange");
        product = program.get<std::string>("--product");
        json_write_path = program.get<std::string>("--log_base_path") + "/config.json";
        tick_func = Omni::get_tick_func(exchange);
        short_sell_unable = program.get<bool>("--short_sell_unable");
    }

    nlohmann::json to_json_obj() const {
        return {
            {"exchange", exchange},
            {"product", product},
            {"min_tick_size", min_tick_size},
            {"lot_size", lot_size},
            {"contract_multiplier", contract_multiplier},
            {"fee_rate_bp", fee_rate_bp},
            {"short_sell_unable", short_sell_unable},
        };
    }
};

std::vector<double> interpolate_params(size_t param_num, double param_lb, double param_ub);

struct OutstandingOrder {
    int64_t price_in_min_ticks;
    int32_t qty_in_lots;
    bool is_bid;
};


class BaseStrategy {
public:
    BaseStrategy(const MarketConfig& market_config);
    virtual ~BaseStrategy() = default;

    void write_config_to_json(const std::string& json_path);

    virtual void make_decision(
        const PriceInfo& price_info,
        bool do_liquidate,
        int32_t position_in_lots,
        const std::map<uint32_t, OutstandingOrder>& outstanding_orders,
        std::map<int64_t, int32_t>& bid_place_orders,
        std::map<int64_t, int32_t>& ask_place_orders,
        std::vector<uint32_t>& bid_cancel_orders,
        std::vector<uint32_t>& ask_cancel_orders
    ) = 0;

protected:
    double min_tick_size_, lot_size_;
    Omni::TickFunc tick_func_;
    bool tick_func_exists_, short_sell_unable_;
    nlohmann::json config_json_obj_;

    int64_t round_price_in_min_ticks(double price);
    int64_t ceil_price_in_min_ticks(double price);
    int64_t floor_price_in_min_ticks(double price);
    double to_double_price(int64_t price_in_min_ticks);

    int32_t round_qty_in_lots(double qty);
    int32_t dollar_to_qty_in_lots(double order_dollar, double order_price);
    double to_double_qty(int32_t qty_in_lots);
};

} // namespace Omni::Trader
