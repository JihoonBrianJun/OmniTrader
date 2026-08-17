#pragma once

#include <memory>
#include <string>
#include <argparse/argparse.hpp>

#include "microprice_factor.hpp"

namespace Omni::Pricer {

// Build one factor instance (factors are stateful per product, so the service calls
// this once per configured product). Returns nullptr for an unknown factor name.
// Mirrors init_strategy: common config in, implementation chosen by name.
std::unique_ptr<BaseFactor> init_factor(
    const FactorConfig& factor_config,
    const argparse::ArgumentParser& program
);

// Registers factor-specific CLI args for the given factor name.
void set_factor_specific_parser(
    const std::string& factor_name, argparse::ArgumentParser& program
);

} // namespace Omni::Pricer
