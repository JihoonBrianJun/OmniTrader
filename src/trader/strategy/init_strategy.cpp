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


std::vector<std::shared_ptr<BaseStrategy>> batch_init_strategy(
    const MarketConfig& market_config,
    const argparse::ArgumentParser& program
) {
    std::vector<std::shared_ptr<BaseStrategy>> strategy_list;
    auto strategy_name = program.get<std::string>("--strategy_name");

    if (strategy_name == "Geuant") {
        std::vector<GeuantParams> params_list;
        batch_init(params_list, program);
        for (const auto& params : params_list) {
            strategy_list.emplace_back(std::make_shared<GeuantStrategy>(market_config, params));
        }
    }

    return strategy_list;
}


void set_strategy_specific_parser(
    const std::string& strategy_name, argparse::ArgumentParser& program
) {
    if (strategy_name == "Geuant") {
        GeuantParams::set_parser(program);
    }
}

} // namespace Omni::Trader
