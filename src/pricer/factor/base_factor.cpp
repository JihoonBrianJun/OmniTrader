#include <algorithm>

#include "base_factor.hpp"

namespace Omni::Pricer {

BaseFactor::BaseFactor(const FactorConfig& config)
:   ema_alpha_(config.ema_alpha),
    cap_bp_(config.cap_bp)
{
}


double BaseFactor::clamp_factor(double factor) const {
    auto cap = cap_bp_ / 10000.0;
    return std::clamp(factor, 1.0 - cap, 1.0 + cap);
}


double BaseFactor::apply_ema(double raw_factor) {
    if (!ema_factor_.has_value() || ema_alpha_ >= 1.0) {
        ema_factor_ = raw_factor;
    } else {
        ema_factor_ = ema_alpha_ * raw_factor + (1.0 - ema_alpha_) * ema_factor_.value();
    }
    return ema_factor_.value();
}

} // namespace Omni::Pricer
