#pragma once

#include "base_strategy.hpp"

namespace Omni::Trader {

struct GeuantParams {
    // Spread determiners
    double spread_const_bp = 0.0;
    double skew_ratio = 0.0;
    bool use_spread_cap = false;
    double spread_cap_bp = 0.0;
    bool use_bbo_cap_buffer = false;
    double bbo_cap_buffer_bp = 0.0;

    // Qty determiners
    int32_t order_lots = 0;
    double order_dollar = 0.0;
    int32_t position_limit_in_lots = 0;
    bool position_in_dollar = false;
    double position_limit_in_dollar = 0.0;

    // Order refresh. How far the price the strategy now wants may sit from an order
    // it already has resting before it bothers replacing it. 0 keeps the original
    // behaviour: replace unless the price matches to the tick.
    double order_buffer_bp = 0.0;

    // Multi order configs
    size_t order_num = 0;
    double order_interval_bp = 0.0;
    bool tick_based_order_interval = false;
    size_t order_interval_ticks = 0;

    static void set_parser(argparse::ArgumentParser& program) {
        program.add_argument("--spread_const_bp").scan<'g', double>().default_value(0.0);
        program.add_argument("--skew_ratio").scan<'g', double>().default_value(0.0);
        program.add_argument("--use_spread_cap").flag();
        program.add_argument("--spread_cap_bp").scan<'g', double>().default_value(0.0);
        program.add_argument("--use_bbo_cap_buffer").flag();
        program.add_argument("--bbo_cap_buffer_bp").scan<'g', double>().default_value(0.0);

        program.add_argument("--order_lots").scan<'i', int32_t>().default_value(int32_t{0});
        program.add_argument("--order_dollar").scan<'g', double>().default_value(0.0);
        program.add_argument("--position_limit_in_lots").scan<'i', int32_t>().default_value(int32_t{0});
        program.add_argument("--position_in_dollar").flag();
        program.add_argument("--position_limit_in_dollar").scan<'g', double>().default_value(0.0);

        program.add_argument("--order_buffer_bp").scan<'g', double>().default_value(0.0);
        program.add_argument("--order_num").scan<'i', size_t>().default_value(size_t{0});
        program.add_argument("--order_interval_bp").scan<'g', double>().default_value(0.0);
        program.add_argument("--tick_based_order_interval").flag();
        program.add_argument("--order_interval_ticks").scan<'i', size_t>().default_value(size_t{0});
    }

    void init(const argparse::ArgumentParser& program) {
        spread_const_bp = program.get<double>("--spread_const_bp");
        skew_ratio = program.get<double>("--skew_ratio");
        use_spread_cap = program.get<bool>("--use_spread_cap");
        spread_cap_bp = program.get<double>("--spread_cap_bp");
        use_bbo_cap_buffer = program.get<bool>("--use_bbo_cap_buffer");
        bbo_cap_buffer_bp = program.get<double>("--bbo_cap_buffer_bp");

        order_lots = program.get<int32_t>("--order_lots");
        order_dollar = program.get<double>("--order_dollar");
        position_limit_in_lots = program.get<int32_t>("--position_limit_in_lots");
        position_in_dollar = program.get<bool>("--position_in_dollar");
        position_limit_in_dollar = program.get<double>("--position_limit_in_dollar");

        order_buffer_bp = program.get<double>("--order_buffer_bp");
        order_num = program.get<size_t>("--order_num");
        order_interval_bp = program.get<double>("--order_interval_bp");
        tick_based_order_interval = program.get<bool>("--tick_based_order_interval");
        order_interval_ticks = program.get<size_t>("--order_interval_ticks");
    }

    // File-based alternative to init(): reads the "strategy_params" section of the
    // trader's third launch-config file (config/<exchange>/strategy0.json). Covers
    // the same fields init() does.
    void parse_json(const nlohmann::json& doc) {
        Omni::Config::JsonSection s(doc, "strategy_params");
        s.get("spread_const_bp", spread_const_bp);
        s.get("skew_ratio", skew_ratio);
        s.get("use_spread_cap", use_spread_cap);
        s.get("spread_cap_bp", spread_cap_bp);
        s.get("use_bbo_cap_buffer", use_bbo_cap_buffer);
        s.get("bbo_cap_buffer_bp", bbo_cap_buffer_bp);

        s.get("order_lots", order_lots);
        s.get("order_dollar", order_dollar);
        s.get("position_limit_in_lots", position_limit_in_lots);
        s.get("position_in_dollar", position_in_dollar);
        s.get("position_limit_in_dollar", position_limit_in_dollar);

        s.get("order_buffer_bp", order_buffer_bp);
        s.get("order_num", order_num);
        s.get("order_interval_bp", order_interval_bp);
        s.get("tick_based_order_interval", tick_based_order_interval);
        s.get("order_interval_ticks", order_interval_ticks);
        s.done();
    }
};

class GeuantStrategy : public BaseStrategy {
public:
    GeuantStrategy(const MarketConfig& market_config, const GeuantParams& params);

    void make_decision(
        const PriceInfo& price_info,
        bool do_liquidate,
        int32_t position_in_lots,
        const std::map<uint64_t, OutstandingOrder>& outstanding_orders,
        std::map<int64_t, int32_t>& bid_place_orders,
        std::map<int64_t, int32_t>& ask_place_orders,
        std::vector<uint64_t>& bid_cancel_orders,
        std::vector<uint64_t>& ask_cancel_orders
    ) override;

private:
    GeuantParams params_;

    double clip_delta(double delta);
    double delta_bid(double position_limit_usage);
    double delta_ask(double position_limit_usage);

    int32_t get_order_qty_in_lots(int64_t price_in_min_ticks, int32_t qty_limit_in_lots);

    // Pair up what is already resting with what the strategy now wants. An
    // outstanding order that has a wanted price within order_buffer_bp of its own
    // keeps its place, and that wanted price is dropped from the place map so no
    // second order is sent for it; everything else outstanding is cancelled.
    //
    // Liquidating turns the buffer off -- see the body for why.
    void choose_orders_to_cancel(
        const std::map<uint64_t, OutstandingOrder>& outstanding_orders,
        std::map<int64_t, int32_t>& bid_place_orders,
        std::map<int64_t, int32_t>& ask_place_orders,
        std::vector<uint64_t>& bid_cancel_orders,
        std::vector<uint64_t>& ask_cancel_orders,
        bool is_liquidate
    );
};

} // namespace Omni::Trader
