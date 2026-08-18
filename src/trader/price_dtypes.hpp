#pragma once

#include <cmath>
#include <cstdint>

namespace Omni::Trader {

// Mirrors orderbook-backtest PriceInfo: what the strategy sees each decision.
// The trader no longer computes fair_price itself — it comes from the `pricer`
// executable — but the struct handed to the strategy is unchanged.
struct PriceInfo {
    int64_t bbid_price_in_min_ticks;
    int64_t bask_price_in_min_ticks;
    double mid_price;
    double fair_price;
    // The factor behind fair_price, as reported by the pricer, or NaN when there was
    // none (the pricer isn't in FACTOR mode, or its fair price was unavailable and we
    // fell back to mid). Logged so a fallback is visible as such.
    double applied_factor = NAN;

    // This product's real price increment on the venue, from the listener's
    // product_info, falling back to the global --min_tick_size until one arrives.
    // Resolved per product by the order handler, because the strategy is one shared
    // instance and cannot hold it.
    //
    // A tick-based order ladder must be spaced on *this*, not on the global unit:
    // levels one global tick apart collapse onto a single price the moment the
    // handler snaps them to a coarser venue grid, and the strategy would believe it
    // had a ladder the exchange never saw. Spacing on the real increment survives
    // that snap -- N real ticks apart before it, N after it.
    double tick_size = 0.0;
};

// Live top-of-book snapshot, maintained by the order handler straight from the
// listener's feed (mirrors the fields of orderbook-backtest L1Tick that the pricer
// consumes). -1 prices = empty side.
struct L1 {
    int64_t bbid_price_in_min_ticks = -1;
    int64_t bask_price_in_min_ticks = -1;
    int32_t bbid_qty_in_lots = 0;
    int32_t bask_qty_in_lots = 0;
};

// Latest fair price received from the `pricer` executable for one product. Kept as a
// single last value rather than a replayable series (live only moves forward), with
// the timestamp the pricer computed it at so a stale value can be aged out.
struct FairPriceState {
    int64_t ts = 0;              // ns, stamped by the pricer
    double fair_price = NAN;     // NaN until the first one arrives
    double factor = NAN;         // NaN unless the pricer runs in FACTOR mode
};

} // namespace Omni::Trader
