#pragma once

#include "pricer_dtypes.hpp"

namespace Omni::Trader {

// Mirrors orderbook-backtest Pricer, adapted for live trading: instead of pulling
// L1 from a matching engine it computes PriceInfo from the latest live L1 snapshot
// maintained by the order handler.
class Pricer {
public:
    Pricer(const PricerConfig& config, double min_tick_size, double lot_size);

    void set_params(double min_tick_size, double lot_size);

    void fetch_fair_price(const L1& l1, PriceInfo& price_info);

private:
    PricerConfig::Mode mode_;
    double min_tick_size_, lot_size_;

    double to_double_price(int64_t price_in_min_ticks);
    double to_double_qty(int32_t qty_in_lots);

    double compute_mid_price(
        int64_t bbid_price_in_min_ticks, int64_t bask_price_in_min_ticks
    );
    double compute_vwap(const L1& l1);
};

} // namespace Omni::Trader
