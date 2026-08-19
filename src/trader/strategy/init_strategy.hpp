#pragma once

#include <memory>
#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>

#include "geuant_strategy.hpp"

namespace Omni::Trader {

std::shared_ptr<BaseStrategy> init_strategy(
    const MarketConfig& market_config,
    const argparse::ArgumentParser& program
);

// JSON counterpart of init_strategy: builds the named strategy from the
// "strategy_params" section of the trader's strategy launch-config file. The name
// comes from trader_config rather than from a parser, so no argparse is involved.
std::shared_ptr<BaseStrategy> init_strategy_from_json(
    const MarketConfig& market_config,
    const std::string& strategy_name,
    const nlohmann::json& doc
);

// Registers strategy-specific CLI args for the given strategy name.
void set_strategy_specific_parser(
    const std::string& strategy_name, argparse::ArgumentParser& program
);

} // namespace Omni::Trader
