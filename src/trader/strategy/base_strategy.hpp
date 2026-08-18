#pragma once

#include <map>
#include <vector>
#include <cmath>
#include <string>
#include <cstdint>
#include <argparse/argparse.hpp>

#include "trader/price_dtypes.hpp"
#include "utils/tick_configs.hpp"

// Mirrors orderbook-backtest base_strategy (MarketConfig / OutstandingOrder /
// BaseStrategy), adapted to the Omni::Trader namespace and live trading.

namespace Omni::Trader {

// Process-global unit configuration. `min_tick_size` and `default_lot_size` are the
// units every internal integer is expressed in: a price is carried as an int64 count
// of min ticks and a quantity as an int32 count of lots, everywhere in the trader.
//
// They are deliberately fixed for the whole run, taken from the CLI and never
// rewritten at runtime. A product's *real* exchange grid can move under a live
// session (a venue can change a filter), and rebasing the normalization unit
// underneath positions, outstanding orders and strategy limits that are already
// denominated in it would silently reinterpret all of them. The real per-product
// grid is applied where it actually has to be -- at order submission, see
// OrderHandler::snap_to_product_grid -- and nowhere else.
//
// `default_lot_size` is named for what it is: the default quantity unit, shared by
// every product, not any one product's lot size.
struct MarketConfig {
    double min_tick_size;
    double default_lot_size;
    std::string exchange;
    Omni::TickFunc tick_func;
    bool short_sell_unable = false;

    static void set_parser(argparse::ArgumentParser& program) {
        program.add_argument("--min_tick_size")
            .scan<'g', double>()
            .default_value(0.0);
        program.add_argument("--default_lot_size")
            .scan<'g', double>()
            .default_value(0.0);
        program.add_argument("--short_sell_unable")
            .flag();
    }

    void init(const argparse::ArgumentParser& program) {
        min_tick_size = program.get<double>("--min_tick_size");
        default_lot_size = program.get<double>("--default_lot_size");
        exchange = program.get<std::string>("--exchange");
        tick_func = Omni::get_tick_func(exchange);
        short_sell_unable = program.get<bool>("--short_sell_unable");
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

    virtual void make_decision(
        const PriceInfo& price_info,
        bool do_liquidate,
        int32_t position_in_lots,
        const std::map<uint64_t, OutstandingOrder>& outstanding_orders,
        std::map<int64_t, int32_t>& bid_place_orders,
        std::map<int64_t, int32_t>& ask_place_orders,
        std::vector<uint64_t>& bid_cancel_orders,
        std::vector<uint64_t>& ask_cancel_orders
    ) = 0;

protected:
    // Copies of the process-global units; fixed for the lifetime of the strategy.
    double min_tick_size_, default_lot_size_;
    Omni::TickFunc tick_func_;
    bool tick_func_exists_, short_sell_unable_;

    int64_t round_price_in_min_ticks(double price);
    int64_t ceil_price_in_min_ticks(double price);
    int64_t floor_price_in_min_ticks(double price);
    double to_double_price(int64_t price_in_min_ticks);

    int32_t round_qty_in_lots(double qty);
    int32_t dollar_to_qty_in_lots(double order_dollar, double order_price);
    double to_double_qty(int32_t qty_in_lots);
};

} // namespace Omni::Trader
