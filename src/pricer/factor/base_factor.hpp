#pragma once

#include <string>
#include <optional>
#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>

#include "pricer/market_state.hpp"
#include "config_handlers/json_config.hpp"

// Mirrors the trader's strategy module (base_strategy / geuant_strategy /
// init_strategy): a common config carried by the base class, per-implementation
// params structs, and an init_factor factory that dispatches on --factor_name.

namespace Omni::Pricer {

// Config common to every factor, the factor-side counterpart of the strategy's
// MarketConfig. Smoothing and clamping are handled here rather than re-implemented by
// each factor, so an implementation only has to produce a raw multiplier.
struct FactorConfig {
    // Identifies the launch config this came from (factor0.json -> "0"). Read from
    // JSON only: FactorConfig shares the pricer's argparse parser, which already
    // registers --name for PricerConfig. Nothing here owns a path, so it is carried
    // for identification rather than used.
    std::string name = "";

    std::string factor_name = "Microprice";

    // EMA smoothing on the raw factor. 1.0 = no smoothing (use the raw value).
    double ema_alpha = 1.0;
    // Hard clamp on how far the factor may pull fair away from mid, in bp. Keeps a
    // momentarily lopsided book from throwing the traders' quotes a long way off.
    double cap_bp = 20.0;

    // Minimal first-pass parser to learn the factor before its own args are
    // registered (two-phase parse, mirroring the trader's strategy handling).
    static void set_header_parser(argparse::ArgumentParser& program) {
        program.add_argument("--factor_name").default_value(std::string("Microprice"));
    }

    static void set_parser(argparse::ArgumentParser& program) {
        set_header_parser(program);
        program.add_argument("--factor_ema_alpha").scan<'g', double>().default_value(1.0);
        program.add_argument("--factor_cap_bp").scan<'g', double>().default_value(20.0);
    }

    void init(const argparse::ArgumentParser& program) {
        factor_name = program.get<std::string>("--factor_name");
        ema_alpha = program.get<double>("--factor_ema_alpha");
        cap_bp = program.get<double>("--factor_cap_bp");
    }

    // File-based alternative to init(): reads the "factor_config" section of the
    // pricer's second launch-config file (config/<exchange>/factor0.json), which
    // carries this alongside the chosen factor's own "factor_params" section.
    void parse_json(const nlohmann::json& doc) {
        Omni::Config::JsonSection s(doc, "factor_config");
        s.get("name", name);
        s.get("factor_name", factor_name);
        s.get("ema_alpha", ema_alpha);
        s.get("cap_bp", cap_bp);
        s.done();
    }
};


// A live factor: given the current market state, produce the multiplier applied to
// mid (`fair_price = factor * mid_price`), matching the semantics of the pre-computed
// factors orderbook-backtest reads from file. Returning nullopt means "not computable
// yet"; the fair-price calculator then falls back to plain mid, exactly as the
// backtest does for a NaN factor.
//
// One instance is held per product, so implementations may keep per-product state
// (e.g. an EMA); compute() is only ever called from the service's task thread.
class BaseFactor {
public:
    explicit BaseFactor(const FactorConfig& config);
    virtual ~BaseFactor() = default;

    virtual std::optional<double> compute(const MarketState& state) = 0;

    // How wide the market is about to be, relative to normal, as of the last
    // compute(). Multiplies the strategy's configured half-spread, so 1.0 -- the
    // default every factor gets until it implements one -- quotes as configured.
    // Read only after a compute() that returned a value.
    virtual double forward_vol() const { return 1.0; }

protected:
    double ema_alpha_, cap_bp_;

    // Clamp a raw multiplier into [1 - cap, 1 + cap].
    double clamp_factor(double factor) const;
    // Fold a raw multiplier into the running EMA and return the smoothed value.
    double apply_ema(double raw_factor);

private:
    // Running EMA; unset until the first computable tick.
    std::optional<double> ema_factor_;
};

} // namespace Omni::Pricer
