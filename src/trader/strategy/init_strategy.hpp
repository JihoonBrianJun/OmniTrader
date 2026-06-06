#pragma once

#include <vector>
#include <memory>
#include <argparse/argparse.hpp>

#include "geuant_strategy.hpp"

namespace Omni::Trader {

std::shared_ptr<BaseStrategy> init_strategy(
    const MarketConfig& market_config,
    const argparse::ArgumentParser& program
);

std::vector<std::shared_ptr<BaseStrategy>> batch_init_strategy(
    const MarketConfig& market_config,
    const argparse::ArgumentParser& program
);

// Registers strategy-specific CLI args for the given strategy name.
void set_strategy_specific_parser(
    const std::string& strategy_name, argparse::ArgumentParser& program
);

} // namespace Omni::Trader
