#pragma once

#include <argparse/argparse.hpp>

#include "base_factor.hpp"

namespace Omni::Pricer {

// Microprice-specific params. Smoothing (--factor_ema_alpha) and the bp clamp
// (--factor_cap_bp) are common to every factor and live in FactorConfig instead.
struct MicropriceParams {
    // Exponent applied to the size weights. 1.0 is plain size weighting; > 1 makes
    // the factor react harder to a lopsided book, < 1 damps it.
    double imbalance_exponent = 1.0;

    static void set_parser(argparse::ArgumentParser& program) {
        program.add_argument("--imbalance_exponent").scan<'g', double>().default_value(1.0);
    }

    void init(const argparse::ArgumentParser& program) {
        imbalance_exponent = program.get<double>("--imbalance_exponent");
    }
};


// Size-weighted top-of-book (microprice) expressed as a multiplier on mid:
//
//     microprice = (bbid * bask_qty^e + bask * bbid_qty^e) / (bask_qty^e + bbid_qty^e)
//     factor     = microprice / mid
//
// Factor > 1 means the book leans bid-heavy (buy pressure), < 1 ask-heavy. With
// e = 1 this is the plain microprice, and the VWAP fair-price mode computes exactly
// the same number directly; running it as a factor instead is what lets it be
// smoothed across ticks and clamped.
class MicropriceFactor : public BaseFactor {
public:
    MicropriceFactor(const FactorConfig& config, const MicropriceParams& params);

    std::optional<double> compute(const MarketState& state) override;

private:
    MicropriceParams params_;
};

} // namespace Omni::Pricer
