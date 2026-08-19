#include "base_strategy.hpp"

namespace Omni::Trader {

BaseStrategy::BaseStrategy(const MarketConfig& market_config)
:   min_tick_size_(market_config.min_tick_size),
    default_lot_size_(market_config.default_lot_size),
    tick_func_(market_config.tick_func),
    tick_func_exists_(tick_func_ != nullptr),
    short_sell_unable_(market_config.short_sell_unable)
{
}


int64_t BaseStrategy::round_price_in_min_ticks(double price) {
    return static_cast<int64_t>(std::round(price / min_tick_size_));
}


int64_t BaseStrategy::ceil_price_in_min_ticks(double price) {
    if (tick_func_exists_) {
        auto local_tick_size = tick_func_(price);
        auto ceil_price = std::ceil(price / local_tick_size - 1e-8) * local_tick_size;
        return round_price_in_min_ticks(ceil_price);
    } else {
        return static_cast<int64_t>(std::ceil(price / min_tick_size_ - 1e-8));
    }
}


int64_t BaseStrategy::floor_price_in_min_ticks(double price) {
    if (tick_func_exists_) {
        auto local_tick_size = tick_func_(price);
        auto floor_price = std::floor(price / local_tick_size + 1e-8) * local_tick_size;
        return round_price_in_min_ticks(floor_price);
    } else {
        return static_cast<int64_t>(std::floor(price / min_tick_size_ + 1e-8));
    }
}


double BaseStrategy::to_double_price(int64_t price_in_min_ticks) {
    return static_cast<double>(price_in_min_ticks) * min_tick_size_;
}


int32_t BaseStrategy::round_qty_in_lots(double qty) {
    return static_cast<int32_t>(std::round(qty / default_lot_size_));
}


int32_t BaseStrategy::dollar_to_qty_in_lots(double order_dollar, double order_price) {
    return round_qty_in_lots(order_dollar / order_price);
}


double BaseStrategy::to_double_qty(int32_t qty_in_lots) {
    return static_cast<double>(qty_in_lots) * default_lot_size_;
}

} // namespace Omni::Trader
