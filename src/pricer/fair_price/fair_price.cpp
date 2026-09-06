#include "fair_price.hpp"

namespace Omni::Pricer {

FairPriceCalculator::FairPriceCalculator(
    FairPriceMode mode, std::unique_ptr<BaseFactor> factor
)
:   mode_(mode),
    factor_(std::move(factor))
{
}


double FairPriceCalculator::compute_vwap(const MarketState& state) const {
    auto total_qty = state.bbid_qty + state.bask_qty;
    if (!(total_qty > 0.0)) return NAN;

    // Weighted by the opposite side's size, as in orderbook-backtest's VWAP mode.
    return (
        state.bbid_price * state.bask_qty + state.bask_price * state.bbid_qty
    ) / total_qty;
}


FairPriceResult FairPriceCalculator::compute(const MarketState& state) {
    FairPriceResult result;

    if (!state.has_two_sided_book()) {
        // Half-empty book: leave everything NaN so nothing is published and traders
        // are not quoted off a one-sided market.
        return result;
    }

    result.mid_price = state.mid_price();

    switch (mode_) {
        case FairPriceMode::MID: {
            result.fair_price = result.mid_price;
            break;
        }
        case FairPriceMode::VWAP: {
            auto vwap = compute_vwap(state);
            result.fair_price = std::isnan(vwap) ? result.mid_price : vwap;
            break;
        }
        case FairPriceMode::FACTOR: {
            // Same application as the backtest: scale mid by the factor, and fall back
            // to plain mid whenever no usable factor is available.
            auto factor = factor_ ? factor_->compute(state) : std::nullopt;
            if (factor.has_value() && std::isfinite(factor.value())) {
                result.factor = factor.value();
                result.fair_price = result.factor * result.mid_price;
                auto forward_vol = factor_->forward_vol();
                // Guarded rather than trusted: a non-positive or non-finite estimate
                // would collapse or poison the strategy's spread, and quoting at the
                // configured width is the right thing to do when the estimate is not
                // usable.
                if (std::isfinite(forward_vol) && forward_vol > 0.0) {
                    result.forward_vol = forward_vol;
                }
            } else {
                result.fair_price = result.mid_price;
            }
            break;
        }
    }

    return result;
}

} // namespace Omni::Pricer
