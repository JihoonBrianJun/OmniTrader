#include <algorithm>
#include <cstdint>
#include "geuant_strategy.hpp"

namespace Omni::Trader {

void batch_init(
    std::vector<GeuantParams>& params_list,
    const argparse::ArgumentParser& program
) {
    params_list.clear();

    auto spread_param_num = program.get<size_t>("--spread_param_num");
    auto spread_const_bp_lb = program.get<double>("--spread_const_bp_lb");
    auto spread_const_bp_ub = program.get<double>("--spread_const_bp_ub");
    auto spread_params = interpolate_params(spread_param_num, spread_const_bp_lb, spread_const_bp_ub);

    auto skew_param_num = program.get<size_t>("--skew_param_num");
    auto skew_ratio_lb = program.get<double>("--skew_ratio_lb");
    auto skew_ratio_ub = program.get<double>("--skew_ratio_ub");
    auto skew_params = interpolate_params(skew_param_num, skew_ratio_lb, skew_ratio_ub);

    for (size_t spread_idx = 0; spread_idx < spread_param_num; ++spread_idx) {
        for (size_t skew_idx = 0; skew_idx < skew_param_num; ++skew_idx) {
            GeuantParams params;
            params.basic_init(program);
            params.core_init(spread_params[spread_idx], skew_params[skew_idx]);
            params_list.emplace_back(params);
        }
    }
}


GeuantStrategy::GeuantStrategy(const MarketConfig& market_config, const GeuantParams& params)
:   BaseStrategy(market_config),
    params_(params)
{
}


double GeuantStrategy::clip_delta(double delta) {
    return params_.use_spread_cap
        ? std::min(std::max(0.0, delta), params_.spread_cap_bp)
        : std::max(0.0, delta);
}


double GeuantStrategy::delta_bid(double position_limit_usage) {
    return clip_delta(
        params_.spread_const_bp * (1.0 + params_.skew_ratio * position_limit_usage)
    );
}


double GeuantStrategy::delta_ask(double position_limit_usage) {
    return clip_delta(
        params_.spread_const_bp * (1.0 - params_.skew_ratio * position_limit_usage)
    );
}


int32_t GeuantStrategy::get_order_qty_in_lots(
    int64_t price_in_min_ticks, int32_t qty_limit_in_lots
) {
    return std::min(
        qty_limit_in_lots,
        params_.position_in_dollar
            ? dollar_to_qty_in_lots(params_.order_dollar, to_double_price(price_in_min_ticks))
            : params_.order_lots
    );
}


void GeuantStrategy::choose_orders_to_cancel(
    const std::map<uint64_t, OutstandingOrder>& outstanding_orders,
    std::map<int64_t, int32_t>& bid_place_orders,
    std::map<int64_t, int32_t>& ask_place_orders,
    std::vector<uint64_t>& bid_cancel_orders,
    std::vector<uint64_t>& ask_cancel_orders
) {
    for (const auto& [cid, outstanding_order] : outstanding_orders) {
        auto& place_orders = outstanding_order.is_bid ? bid_place_orders : ask_place_orders;
        auto price_it = place_orders.find(outstanding_order.price_in_min_ticks);

        if (price_it == place_orders.end()) {
            auto& cancel_orders = outstanding_order.is_bid ? bid_cancel_orders : ask_cancel_orders;
            cancel_orders.emplace_back(cid);
        } else {
            place_orders.erase(price_it);
        }
    }
}


void GeuantStrategy::make_decision(
    const PriceInfo& price_info,
    bool do_liquidate,
    int32_t position_in_lots,
    const std::map<uint64_t, OutstandingOrder>& outstanding_orders,
    std::map<int64_t, int32_t>& bid_place_orders,
    std::map<int64_t, int32_t>& ask_place_orders,
    std::vector<uint64_t>& bid_cancel_orders,
    std::vector<uint64_t>& ask_cancel_orders
) {
    if (std::isnan(price_info.mid_price)) return;

    if (do_liquidate) {
        if (position_in_lots > 0) {
            ask_place_orders[
                floor_price_in_min_ticks(to_double_price(price_info.bask_price_in_min_ticks-1))
            ] = position_in_lots;
        } else if (position_in_lots < 0) {
            bid_place_orders[
                ceil_price_in_min_ticks(to_double_price(price_info.bbid_price_in_min_ticks+1))
            ] = -position_in_lots;
        }

        choose_orders_to_cancel(
            outstanding_orders, bid_place_orders, ask_place_orders, bid_cancel_orders, ask_cancel_orders
        );
        return;
    }

    auto bid_price_limit_in_min_ticks = params_.use_bbo_cap_buffer
        ? floor_price_in_min_ticks(
            to_double_price(price_info.bbid_price_in_min_ticks) * (1.0 - params_.bbo_cap_buffer_bp / 10000.0)
        )
        : floor_price_in_min_ticks(to_double_price(price_info.bask_price_in_min_ticks-1));
    auto ask_price_limit_in_min_ticks = params_.use_bbo_cap_buffer
        ? ceil_price_in_min_ticks(
            to_double_price(price_info.bask_price_in_min_ticks) * (1.0 + params_.bbo_cap_buffer_bp / 10000.0)
        )
        : ceil_price_in_min_ticks(to_double_price(price_info.bbid_price_in_min_ticks+1));

    auto bid_qty_limit_in_lots = params_.position_in_dollar
        ? (dollar_to_qty_in_lots(params_.position_limit_in_dollar, price_info.mid_price) - position_in_lots)
        : (params_.position_limit_in_lots - position_in_lots);
    auto ask_qty_limit_in_lots = std::min(
        short_sell_unable_ ? position_in_lots : INT32_MAX,
        params_.position_in_dollar
            ? (position_in_lots + dollar_to_qty_in_lots(params_.position_limit_in_dollar, price_info.mid_price))
            : (position_in_lots + params_.position_limit_in_lots)
    );

    auto position_limit_usage = params_.position_in_dollar
        ? to_double_qty(position_in_lots) * price_info.mid_price / params_.position_limit_in_dollar
        : to_double_qty(position_in_lots) / to_double_qty(params_.position_limit_in_lots);
    auto base_delta_bid = delta_bid(position_limit_usage);
    auto base_delta_ask = delta_ask(position_limit_usage);

    for (size_t order_idx = 0; order_idx < params_.order_num; ++order_idx) {
        auto price_offset = ((order_idx >= 1) && params_.tick_based_order_interval)
            ? static_cast<double>(order_idx * params_.order_interval_ticks) * (
                tick_func_exists_ ? tick_func_(price_info.fair_price) : min_tick_size_
            )
            : price_info.fair_price * static_cast<double>(order_idx) * params_.order_interval_bp / 10000.0;

        if (bid_qty_limit_in_lots > 0) {
            auto bid_price_in_min_ticks = std::min(
                round_price_in_min_ticks(
                    ((order_idx >= 1) && params_.tick_based_order_interval)
                        ? to_double_price(bid_place_orders.rbegin()->first) - price_offset
                        : price_info.fair_price * (1.0 - base_delta_bid / 10000.0) - price_offset
                ), bid_price_limit_in_min_ticks
            );
            bid_place_orders[bid_price_in_min_ticks] = get_order_qty_in_lots(
                bid_price_in_min_ticks, bid_qty_limit_in_lots
            );
        }
        if (ask_qty_limit_in_lots > 0) {
            auto ask_price_in_min_ticks = std::max(
                round_price_in_min_ticks(
                    ((order_idx >= 1) && params_.tick_based_order_interval)
                        ? to_double_price(ask_place_orders.begin()->first) + price_offset
                        : price_info.fair_price * (1.0 + base_delta_ask / 10000.0) + price_offset
                ), ask_price_limit_in_min_ticks
            );
            ask_place_orders[ask_price_in_min_ticks] = get_order_qty_in_lots(
                ask_price_in_min_ticks, ask_qty_limit_in_lots
            );
        }
    }

    choose_orders_to_cancel(
        outstanding_orders, bid_place_orders, ask_place_orders, bid_cancel_orders, ask_cancel_orders
    );
}

} // namespace Omni::Trader
