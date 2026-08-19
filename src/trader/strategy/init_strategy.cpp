#include "init_strategy.hpp"

namespace Omni::Trader {

std::shared_ptr<BaseStrategy> init_strategy(
    const MarketConfig& market_config,
    const argparse::ArgumentParser& program
) {
    auto strategy_name = program.get<std::string>("--strategy_name");

    if (strategy_name == "Geuant") {
        GeuantParams params;
        params.init(program);
        return std::make_shared<GeuantStrategy>(market_config, params);
    }
    return nullptr;
}


std::shared_ptr<BaseStrategy> init_strategy_from_json(
    const MarketConfig& market_config,
    const std::string& strategy_name,
    const nlohmann::json& doc
) {
    if (strategy_name == "Geuant") {
        GeuantParams params;
        params.parse_json(doc);
        return std::make_shared<GeuantStrategy>(market_config, params);
    }
    return nullptr;
}


void set_strategy_specific_parser(
    const std::string& strategy_name, argparse::ArgumentParser& program
) {
    if (strategy_name == "Geuant") {
        GeuantParams::set_parser(program);
    }
}

} // namespace Omni::Trader
