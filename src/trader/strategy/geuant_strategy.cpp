#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>   // std::abs for integral types (<cmath> only guarantees the floating ones)
#include "geuant_strategy.hpp"

namespace Omni::Trader {

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
    std::vector<uint64_t>& ask_cancel_orders,
    bool is_liquidate
) {
    // No buffer while liquidating. The liquidation quote is sized to the whole
    // position, so letting a nearby resting order stand in for it would leave the
    // position only partly quoted out -- that order carries whatever size it was
    // placed with, not the size needed to get flat.
    const double buffer_bp = is_liquidate ? 0.0 : params_.order_buffer_bp;

    for (const auto& [cid, outstanding_order] : outstanding_orders) {
        auto& place_orders = outstanding_order.is_bid ? bid_place_orders : ask_place_orders;
        const auto outstanding_price = outstanding_order.price_in_min_ticks;

        // How far a wanted price may sit from this order and still count as the same
        // quote, as a whole number of min ticks -- which is what the place map is
        // keyed by, so the matching below is pure integer arithmetic.
        //
        // Computed straight from the tick count rather than by converting to a price
        // and back: to_double_price() multiplies by min_tick_size_ and this would
        // immediately divide by it again, and min_tick_size_ is not exactly
        // representable in binary, so the round trip only adds rounding to undo.
        //
        // The epsilon is load-bearing, not decoration. Whenever the exact result
        // lands on an integer -- which is what a hand-written buffer like 1.0 or 0.5
        // tends to produce -- the division can land a hair below it, and a bare
        // floor() would then quietly shrink the tolerance by a full tick.
        //
        // Flooring is also what makes buffer_bp = 0 exactly the original behaviour: a
        // tolerance below one tick rounds to zero, and the range query degenerates to
        // the single exact key.
        int64_t tolerance_in_min_ticks = 0;
        if (buffer_bp > 0.0) {
            tolerance_in_min_ticks = static_cast<int64_t>(std::floor(
                static_cast<double>(std::abs(outstanding_price)) * buffer_bp / 10000.0 + 1e-8
            ));
        }

        // The nearest wanted price -- not merely a nearby one. With a ladder
        // (order_num > 1) two wanted prices can both be in range, and pairing each
        // resting order with the closest keeps the ladder's shape instead of
        // collapsing two levels onto one.
        //
        // The map is sorted, so the nearest key is always one of the two bracketing
        // this order: the first at or above it, and the one before that. Checking
        // just those two is O(log n) and needs no scan of the range.
        auto upper = place_orders.lower_bound(outstanding_price);
        auto candidate = place_orders.end();
        int64_t best_distance = 0;

        // Below first, so an exact tie between the two neighbours resolves to the
        // lower price -- the side a resting bid would rather keep.
        if (upper != place_orders.begin()) {
            candidate = std::prev(upper);
            best_distance = outstanding_price - candidate->first;   // > 0 by ordering
        }
        if (upper != place_orders.end()) {
            auto distance = upper->first - outstanding_price;       // >= 0 by ordering
            if (candidate == place_orders.end() || distance < best_distance) {
                candidate = upper;
                best_distance = distance;
            }
        }
        // Near, but near enough?
        if (candidate != place_orders.end() && best_distance > tolerance_in_min_ticks) {
            candidate = place_orders.end();
        }

        if (candidate == place_orders.end()) {
            auto& cancel_orders = outstanding_order.is_bid ? bid_cancel_orders : ask_cancel_orders;
            cancel_orders.emplace_back(cid);
        } else {
            // Close enough: the resting order stands in for this quote, so drop the
            // wanted price rather than sending a second order at nearly the same
            // level. Each wanted price is claimed by at most one resting order.
            place_orders.erase(candidate);
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

    // "One tick" means one of *this product's* ticks everywhere below: the caps that
    // hold a quote inside the touch, the liquidation prices, and the ladder spacing.
    // Stepping by the global min tick instead shrinks every one of those steps to
    // nothing as --min_tick_size is made finer -- the "one tick inside the ask" cap
    // becomes "at the ask", and a quote meant to be passive crosses the spread.
    //
    // tick_func wins where it exists (a venue whose increment is a function of price
    // rather than a per-product constant). The global unit is the last resort, for
    // the window before any product_info has arrived.
    const auto local_tick_size = tick_func_exists_
        ? tick_func_(price_info.fair_price)
        : (price_info.tick_size > 0.0 ? price_info.tick_size : min_tick_size_);

    if (do_liquidate) {
        if (position_in_lots > 0) {
            ask_place_orders[
                floor_price_in_min_ticks(
                    to_double_price(price_info.bask_price_in_min_ticks) - local_tick_size
                )
            ] = position_in_lots;
        } else if (position_in_lots < 0) {
            bid_place_orders[
                ceil_price_in_min_ticks(
                    to_double_price(price_info.bbid_price_in_min_ticks) + local_tick_size
                )
            ] = -position_in_lots;
        }

        choose_orders_to_cancel(
            outstanding_orders, bid_place_orders, ask_place_orders,
            bid_cancel_orders, ask_cancel_orders, do_liquidate
        );
        return;
    }

    auto bid_price_limit_in_min_ticks = params_.use_bbo_cap_buffer
        ? floor_price_in_min_ticks(
            to_double_price(price_info.bbid_price_in_min_ticks) * (1.0 - params_.bbo_cap_buffer_bp / 10000.0)
        )
        : floor_price_in_min_ticks(
            to_double_price(price_info.bask_price_in_min_ticks) - local_tick_size
          );
    auto ask_price_limit_in_min_ticks = params_.use_bbo_cap_buffer
        ? ceil_price_in_min_ticks(
            to_double_price(price_info.bask_price_in_min_ticks) * (1.0 + params_.bbo_cap_buffer_bp / 10000.0)
        )
        : ceil_price_in_min_ticks(
            to_double_price(price_info.bbid_price_in_min_ticks) + local_tick_size
          );

    auto position_limit_in_lots = params_.position_in_dollar
        ? dollar_to_qty_in_lots(params_.position_limit_in_dollar, price_info.mid_price)
        : params_.position_limit_in_lots;

    auto bid_qty_limit_in_lots = position_limit_in_lots - position_in_lots;
    auto ask_qty_limit_in_lots = std::min(
        short_sell_unable_ ? position_in_lots : INT32_MAX,
        position_in_lots + position_limit_in_lots
    );

    auto position_limit_usage = params_.position_in_dollar
        ? to_double_qty(position_in_lots) * price_info.mid_price / params_.position_limit_in_dollar
        : to_double_qty(position_in_lots) / to_double_qty(params_.position_limit_in_lots);
    auto base_delta_bid = delta_bid(position_limit_usage);
    auto base_delta_ask = delta_ask(position_limit_usage);

    for (size_t order_idx = 0; order_idx < params_.order_num; ++order_idx) {
        // order_interval_ticks likewise counts this product's ticks.
        auto price_offset = ((order_idx >= 1) && params_.tick_based_order_interval)
            ? static_cast<double>(order_idx * params_.order_interval_ticks) * local_tick_size
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
        outstanding_orders, bid_place_orders, ask_place_orders,
        bid_cancel_orders, ask_cancel_orders, do_liquidate
    );
}

} // namespace Omni::Trader
