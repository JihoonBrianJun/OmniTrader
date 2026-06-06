#pragma once

#include <cstdint>

namespace Omni::Trader {

// Mirrors orderbook-backtest PriceInfo.
struct PriceInfo {
    int64_t bbid_price_in_min_ticks;
    int64_t bask_price_in_min_ticks;
    double mid_price;
    double fair_price;
};

// Live top-of-book snapshot fed to the pricer (mirrors the fields of
// orderbook-backtest L1Tick that the pricer consumes). -1 prices = empty side.
struct L1 {
    int64_t bbid_price_in_min_ticks = -1;
    int64_t bask_price_in_min_ticks = -1;
    int32_t bbid_qty_in_lots = 0;
    int32_t bask_qty_in_lots = 0;
};

} // namespace Omni::Trader
