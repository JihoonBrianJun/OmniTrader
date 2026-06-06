#include <cmath>
#include "interface.hpp"

namespace Omni::Strategy {

IStrategy::IStrategy(const StrategyConfig& config, quill::Logger* logger)
:   logger_(logger),
    min_tick_size_(config.min_tick_size),
    lot_size_(config.lot_size)
{
}


long IStrategy::get_price_in_min_ticks(double price) {
    return static_cast<long>(std::round(price / min_tick_size_));
}


long IStrategy::get_qty_in_lots(double qty) {
    return static_cast<long>(std::round(qty / lot_size_));
}

} // namespace Omni::Strategy
