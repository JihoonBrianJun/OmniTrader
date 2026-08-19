#pragma once

#include <memory>
#include <string>
#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>

#include "microprice_factor.hpp"

namespace Omni::Pricer {

// Build one factor instance (factors are stateful per product, so the service calls
// this once per configured product). Returns nullptr for an unknown factor name.
// Mirrors init_strategy: common config in, implementation chosen by name.
std::unique_ptr<BaseFactor> init_factor(
    const FactorConfig& factor_config,
    const argparse::ArgumentParser& program
);

// JSON counterpart of init_factor: builds the factor named by `factor_config` from
// the "factor_params" section of the pricer's factor launch-config file.
std::unique_ptr<BaseFactor> init_factor_from_json(
    const FactorConfig& factor_config,
    const nlohmann::json& doc
);

// Registers factor-specific CLI args for the given factor name.
void set_factor_specific_parser(
    const std::string& factor_name, argparse::ArgumentParser& program
);

} // namespace Omni::Pricer
