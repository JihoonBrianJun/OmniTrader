#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <string_view>

#include "pricer/market_state.hpp"
#include "pricer/factor/base_factor.hpp"

namespace Omni::Pricer {

// How fair price is derived from the book. Mirrors orderbook-backtest's
// PricerConfig::Mode. This is the seam to extend as more fair-price logics arrive:
// a new one is an enum value, a case in FairPriceCalculator::compute, and a choice in
// PricerConfig's parser. Anything that needs its own params/state should be written
// as a factor instead (see factor/base_factor.hpp) and used through FACTOR.
enum class FairPriceMode {
    MID,       // fair = mid. The default: no factor, no configuration, always available.
    VWAP,      // fair = size-weighted top-of-book
    FACTOR     // fair = factor * mid, with the factor from the configured BaseFactor
};

inline FairPriceMode fair_price_mode_from_string(std::string_view mode) {
    if (mode == "VWAP") return FairPriceMode::VWAP;
    if (mode == "FACTOR") return FairPriceMode::FACTOR;
    return FairPriceMode::MID;
}

inline std::string_view fair_price_mode_name(FairPriceMode mode) {
    switch (mode) {
        case FairPriceMode::VWAP: return "VWAP";
        case FairPriceMode::FACTOR: return "FACTOR";
        default: return "MID";
    }
}


// What one publish tick produces for one product. All NaN means "not priceable yet"
// (no two-sided book), in which case nothing is published and the trader keeps
// falling back to its own mid.
struct FairPriceResult {
    double mid_price = NAN;
    double fair_price = NAN;
    // The factor folded into fair_price, or NaN if none was (non-FACTOR mode, or the
    // factor was not computable). Published purely as a diagnostic.
    double factor = NAN;
    // The factor's view of how wide the market is about to be, as a multiplier on the
    // strategy's quoted half-spread: 1.0 means "as configured", 2.0 "twice as wide".
    // Only FACTOR mode produces one -- MID and VWAP carry no state to estimate it
    // from and leave this at 1.0, which is exactly the pre-existing behaviour. It is
    // also left at 1.0 whenever the factor was not computable, so a fallback to plain
    // mid falls back on the spread as well rather than scaling by a stale estimate.
    double forward_vol = 1.0;

    bool priceable() const { return !std::isnan(fair_price); }
};


// The fair-price half of the pricer, moved here from the trader: the trader used to
// hold this and compute fair price in-process off its own L1, which meant every
// trader re-derived the same number and the factor had to travel to each of them.
// Now it is computed once, here, and the fair price itself is what goes on the wire.
//
// One instance per product (factors are stateful, so the calculator that owns one is
// too). Called only from the service's task thread.
class FairPriceCalculator {
public:
    FairPriceCalculator(FairPriceMode mode, std::unique_ptr<BaseFactor> factor);

    FairPriceResult compute(const MarketState& state);

private:
    FairPriceMode mode_;
    // Only populated in FACTOR mode; null otherwise.
    std::unique_ptr<BaseFactor> factor_;

    double compute_vwap(const MarketState& state) const;
};

} // namespace Omni::Pricer
