#pragma once

#include <cmath>
#include <cstdint>

namespace Omni::Pricer {

// Latest normalized market state for one product, accumulated by the pricer service
// from the listener's feed. It is the single input to both layers of the pricer: the
// fair-price calculator and (in FACTOR mode) the factor it drives.
//
// Everything is in raw exchange units (not ticks/lots). The pricer never rounds to a
// tick: it publishes a fair price as a plain double, and the trader's strategy is what
// converts to tick/lot units, so the pricer needs no product info at all.
struct MarketState {
    int64_t ts = 0;              // ns, when this state was last updated

    double bbid_price = NAN;
    double bask_price = NAN;
    double bbid_qty = 0.0;
    double bask_qty = 0.0;

    // Latest trade print, for factors that use trade flow. NaN until one arrives.
    double last_trade_price = NAN;
    double cum_trade_qty = NAN;
    double cum_buy_trade_qty = NAN;

    bool has_two_sided_book() const {
        return !std::isnan(bbid_price) && !std::isnan(bask_price)
            && bbid_price > 0.0 && bask_price > 0.0;
    }

    double mid_price() const {
        return (bbid_price + bask_price) / 2.0;
    }
};

} // namespace Omni::Pricer
