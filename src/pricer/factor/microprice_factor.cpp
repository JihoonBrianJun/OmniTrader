#include <cmath>

#include "microprice_factor.hpp"

namespace Omni::Pricer {

MicropriceFactor::MicropriceFactor(
    const FactorConfig& config, const MicropriceParams& params
)
:   BaseFactor(config),
    params_(params)
{
}


std::optional<double> MicropriceFactor::compute(const MarketState& state) {
    if (!state.has_two_sided_book()) return std::nullopt;

    auto mid = state.mid_price();
    if (mid <= 0.0) return std::nullopt;

    // Weighted by the *opposite* side's size: a large bid queue pulls the microprice
    // up toward the ask, which is the usual imbalance convention.
    auto bbid_weight = std::pow(state.bask_qty, params_.imbalance_exponent);
    auto bask_weight = std::pow(state.bbid_qty, params_.imbalance_exponent);
    auto total_weight = bbid_weight + bask_weight;
    if (!(total_weight > 0.0)) return std::nullopt;

    auto microprice = (
        state.bbid_price * bbid_weight + state.bask_price * bask_weight
    ) / total_weight;

    return apply_ema(clamp_factor(microprice / mid));
}

} // namespace Omni::Pricer
